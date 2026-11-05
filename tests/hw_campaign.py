"""Serial campaign for the ESP32-S3 test firmware; it never flashes the board."""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial


class Campaign:
    def __init__(self, port: str, mac: str, baud: int = 115200):
        self.ser = serial.Serial(port, baudrate=baud, timeout=0.25, write_timeout=2)
        self.mac = mac.upper()
        self.ranges = []
        self.restarts = 0
        self.lines = []
        self.reset()

    def reset(self):
        # USB-UART reset is a warm reboot and does not remove board power.
        self.ser.reset_input_buffer()
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        self.ser.dtr = True

    def read_until(self, predicate, timeout=35):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if not line:
                continue
            print(line)
            self.lines.append(line)
            if predicate(line):
                return line
        raise TimeoutError("serial response timeout")

    def ready(self):
        line = self.read_until(lambda s: s.startswith("READY,"))
        if f"mac={self.mac}" not in line or "partition=avtest" not in line:
            raise AssertionError(f"wrong device/test partition: {line}")

    def command(self, command, response_prefix, timeout=20):
        self.ser.write((command + "\n").encode("ascii"))
        self.ser.flush()
        return self.read_until(lambda s: s.startswith(response_prefix), timeout)

    def status(self, command, label, expected):
        line = self.command(command, label + ",")
        m = re.search(r"status=(\d+)", line)
        if not m or int(m.group(1)) != expected:
            raise AssertionError(f"{command}: expected {label} status {expected}: {line}")
        return line

    def reserve(self, count, expected_status=0):
        line = self.status(f"R {count}", "RESERVE", expected_status)
        if expected_status == 5:
            if "esp_err=0x0" in line:
                raise AssertionError(f"failed reservation lacks native ESP error: {line}")
        if expected_status:
            timeout = self.ser.timeout
            self.ser.timeout = 0.2
            raw = self.ser.readline()
            self.ser.timeout = timeout
            if raw.decode("utf-8", "replace").strip().startswith("RANGE,"):
                raise AssertionError("failed reservation emitted a value range")
            return
        r = self.read_until(lambda s: s.startswith("RANGE,"))
        _, first, last = r.split(",")
        start, end = int(first), int(last)
        if end - start + 1 != count:
            raise AssertionError(f"wrong range size: {r}")
        for old_start, old_end in self.ranges:
            if start <= old_end and old_start <= end:
                raise AssertionError(f"overlapping ranges {(start, end)} and {(old_start, old_end)}")
        self.ranges.append((start, end))

    def reboot(self):
        self.ser.write(b"REBOOT\n")
        self.ser.flush()
        time.sleep(0.4)
        self.ready()
        self.restarts += 1

    def run(self):
        self.ready()
        # Strict security state: absent data blocks, explicit test provisioning opens it.
        self.status("L", "SECURITY_LOAD", 4)
        self.status("PROVISION_TEST", "SECURITY_PROVISION_TEST_IDENTITY", 0)
        self.status("L", "SECURITY_LOAD", 0)
        self.command("DROP_S0", "DROP,")
        self.status("L", "SECURITY_LOAD", 4)
        self.reboot()
        self.status("L", "SECURITY_LOAD", 4)
        self.command("DROP_S1", "DROP,")
        self.status("PROVISION_TEST", "SECURITY_PROVISION_TEST_IDENTITY", 0)
        self.command("CORRUPT_S0", "CORRUPT,")
        self.status("L", "SECURITY_LOAD", 4)
        self.reboot()
        self.status("L", "SECURITY_LOAD", 4)

        # Counter damaged/missing checks happen before any value is emitted.
        self.command("CORRUPT_C0", "CORRUPT,")
        self.reserve(1, expected_status=4)
        self.command("DROP_C0", "DROP,")
        self.command("DROP_C1", "DROP,")
        self.status("PROVISION_COUNTER_TEST", "COUNTER_PROVISION_TEST_IDENTITY", 0)
        self.command("DROP_C0", "DROP,")
        self.reserve(1, expected_status=4)
        self.command("DROP_C1", "DROP,")
        self.status("PROVISION_COUNTER_TEST", "COUNTER_PROVISION_TEST_IDENTITY", 0)

        self.reserve(3)
        self.reboot()
        self.reserve(5)
        self.reboot()
        self.reserve(2)

        # Fill only the isolated avtest partition, then prove no range is returned.
        self.command("FILL", "FILL,", timeout=90)
        fail = self.status("R 1", "RESERVE", 5)
        if "operation=nvs_set_blob" not in fail and "operation=nvs_commit" not in fail:
            raise AssertionError(f"NVS error operation not reported: {fail}")
        return {"port": self.ser.port, "mac": self.mac, "ranges": self.ranges,
                "restart_count": self.restarts, "nvs_full_result": fail,
                "physical_power_cut": "UNVERIFIED"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--expect-mac", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    c = Campaign(args.port, args.expect_mac)
    try:
        result = c.run()
    finally:
        c.ser.close()
    blob = json.dumps(result, indent=2)
    print(blob)
    if args.output:
        args.output.write_text(blob + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
