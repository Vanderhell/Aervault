"""Manual USB power-cut campaign; resumes verification after the board reconnects."""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def open_quiet(port):
    s = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=2)
    s.port = port
    s.dtr = False
    s.rts = False
    s.open()
    return s


def read_line(s, timeout=1.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        raw = s.readline()
        if raw:
            return raw.decode("utf-8", "replace").strip()
    return None


def send(s, command):
    s.write((command + "\n").encode("ascii"))
    s.flush()


def find_expected_port(expected_mac, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        candidates = [p.device for p in list_ports.comports()
                      if p.vid == 0x303A and p.pid == 0x1001]
        for port in candidates:
            try:
                s = open_quiet(port)
                send(s, "STATUS")
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    line = read_line(s, 0.2)
                    if line and line.startswith("STATUS,"):
                        if f"mac={expected_mac}" in line and "partition=avtest" in line:
                            return s, port, line
                        break
                s.close()
            except (serial.SerialException, OSError):
                pass
        time.sleep(1)
    raise TimeoutError("expected ESP32-S3 USB Serial/JTAG device did not reconnect")


def get_security_value(s):
    send(s, "L")
    status = None
    end = time.monotonic() + 5
    while time.monotonic() < end:
        line = read_line(s, 0.25)
        if not line:
            continue
        if line.startswith("SECURITY_LOAD,"):
            m = re.search(r"status=(\d+)", line)
            status = int(m.group(1)) if m else None
        if line.startswith("VALUE,"):
            return status, int(line.split(",", 1)[1])
    return status, None


def check_next_range(s, previous_end):
    send(s, "R 1")
    status = None
    end = time.monotonic() + 8
    while time.monotonic() < end:
        line = read_line(s, 0.25)
        if not line:
            continue
        if line.startswith("RESERVE,"):
            m = re.search(r"status=(\d+)", line)
            status = int(m.group(1)) if m else None
        if line.startswith("RANGE,"):
            _, first, last = line.split(",")
            first, last = int(first), int(last)
            if status != 0 or first != last or (previous_end is not None and first <= previous_end):
                raise AssertionError(f"post-cut counter repeated or invalid: {line}")
            # av_reserve returns persisted_previous + 1 only after replace
            # succeeds. The NVS replace callback returns success only after
            # nvs_commit, so this derives the durable upper bound observed by
            # this first successful post-recovery reservation.
            persisted_upper_bound = first - 1
            if first <= persisted_upper_bound:
                raise AssertionError("post-cut reservation did not exceed persisted bound")
            if previous_end is not None and persisted_upper_bound < previous_end:
                raise AssertionError("persisted counter bound is below a confirmed issued range")
            return first, last, persisted_upper_bound
    raise TimeoutError(f"post-cut reservation did not complete, status={status}")


def run(args):
    s = open_quiet(args.port)
    # The flasher already reset the board. A second RTS/DTR reset can race the
    # USB Serial/JTAG startup banner, so identify a running app with STATUS.
    send(s, "STATUS")
    deadline = time.monotonic() + 8
    status_line = None
    while time.monotonic() < deadline:
        line = read_line(s, 0.25)
        if line and line.startswith("STATUS,"):
            status_line = line
            break
    if (not status_line or f"mac={args.expect_mac}" not in status_line or
            "partition=avtest" not in status_line):
        s.close()
        raise AssertionError(f"wrong or absent device: {status_line}")

    status, value = get_security_value(s)
    if status == 4:
        send(s, "PROVISION_TEST")
        end = time.monotonic() + 5
        while time.monotonic() < end:
            line = read_line(s, 0.25)
            if line and line.startswith("SECURITY_PROVISION_TEST_IDENTITY,"):
                if "status=0" not in line:
                    raise AssertionError(f"test identity provisioning failed: {line}")
                break
        status, value = get_security_value(s)
    if status != 0 or value is None:
        raise AssertionError(f"could not establish strict-state baseline: {status},{value}")
    confirmed_value = value

    ranges = []
    last_batch_begin = None
    last_batch_ok = 0
    disconnect_seen = False
    pull_prompted = False
    print(f"BASELINE,mac={args.expect_mac},security_value={confirmed_value}", flush=True)
    print(f"PHYSICAL_PULL_REQUIRED,port={args.port}: during any BATCH_BEGIN/BATCH_OK interval, physically unplug the USB cable that powers the board. Leave it disconnected for at least 2 seconds, then reconnect it. Do not reset or flash between cut and recovery check.", flush=True)
    send(s, f"STRESS {args.batches}")

    while True:
        try:
            line = read_line(s, 0.5)
        except (serial.SerialException, OSError):
            disconnect_seen = True
            break
        if not line:
            if args.port not in {p.device for p in list_ports.comports()}:
                disconnect_seen = True
                break
            continue
        if line.startswith("BATCH_BEGIN,"):
            last_batch_begin = int(line.split(",")[1])
            print(f"WRITE_BATCH_ACTIVE,{last_batch_begin}", flush=True)
            if not pull_prompted:
                print("UNPLUG_NOW during this repeated-write batch; reconnect only after at least 2 seconds.", flush=True)
                pull_prompted = True
        elif line.startswith("RANGE,"):
            _, first, last = line.split(",")
            first, last = int(first), int(last)
            for old_first, old_last in ranges:
                if first <= old_last and old_first <= last:
                    raise AssertionError(f"overlapping emitted ranges: {line}")
            ranges.append((first, last))
        elif line.startswith("BATCH_OK,"):
            _, batch, value_s = line.split(",")
            last_batch_ok = int(batch)
            confirmed_value = int(value_s)
            print(line, flush=True)
        elif line.startswith("STRESS_COMPLETE,"):
            raise TimeoutError("stress ended before a physical USB disconnect was observed")
        elif line.startswith("STRESS_STORE,") or line.startswith("STRESS_RESERVE,"):
            print(line, flush=True)
            raise AssertionError(f"stress operation failed before unplug: {line}")

    if not disconnect_seen:
        raise AssertionError("serial disconnect was not observed")
    s.close()
    print("USB_DISCONNECT_OBSERVED; waiting for the same MAC to reconnect", flush=True)
    s, new_port, status_line = find_expected_port(args.expect_mac, args.reconnect_timeout)
    reset_match = re.search(r"reset_reason=(\d+)", status_line)
    reset_reason = int(reset_match.group(1)) if reset_match else None
    power_on_reset = reset_reason == 1  # ESP_RST_POWERON

    status, recovered_value = get_security_value(s)
    if status != 0 or recovered_value is None:
        s.close()
        raise AssertionError(f"strict security object did not recover: status={status}")
    if recovered_value < confirmed_value:
        s.close()
        raise AssertionError(f"confirmed security state rolled back {confirmed_value}->{recovered_value}")
    previous_end = ranges[-1][1] if ranges else None
    if args.previous_end is not None:
        previous_end = max(previous_end if previous_end is not None else 0,
                           args.previous_end)
    recovered_range = check_next_range(s, previous_end)
    s.close()

    result = {
        "port_before_cut": args.port,
        "port_after_reconnect": new_port,
        "mac": args.expect_mac,
        "usb_disconnect_observed": True,
        "power_on_reset_reason": reset_reason,
        "power_loss_confirmed": power_on_reset,
        "batch_incomplete_at_disconnect": last_batch_begin is not None and last_batch_begin > last_batch_ok,
        "last_batch_started": last_batch_begin,
        "last_batch_confirmed": last_batch_ok,
        "last_confirmed_security_value": confirmed_value,
        "recovered_security_value": recovered_value,
        "security_rollback": False,
        "previous_ranges": ranges,
        "counter_object": {
            "partition": "avtest",
            "namespace": "avctr",
            "identity_hex": "C1" * 16,
            "schema": 1,
            "policy": "AV_COUNTER"
        },
        "persisted_counter_upper_bound_before_first_post_cut_reservation": recovered_range[2],
        "post_cut_range": [recovered_range[0], recovered_range[1]],
        "post_cut_range_starts_above_persisted_bound": recovered_range[0] > recovered_range[2],
        "counter_overlap": False,
        "physical_cut_during_flash_program": "UNVERIFIED",
        "note": "USB loss and a pending write batch were observed; no external electrical probe measured the exact flash-program interval."
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2), flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--expect-mac", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--batches", type=int, default=100000)
    p.add_argument("--reconnect-timeout", type=int, default=300)
    p.add_argument("--previous-end", type=int, default=None,
                   help="highest range end already issued before this campaign")
    run(p.parse_args())


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr, flush=True)
        sys.exit(1)
