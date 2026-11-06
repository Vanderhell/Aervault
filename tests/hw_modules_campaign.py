"""Focused ESP32-S3 WAL/queue/history/diagnostics/budget campaign over avraw.

The campaign only formats the dedicated avraw partition. It does not erase NVS,
run unrelated module campaigns, or claim a power-cut-at-program-time result.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial


class Board:
    def __init__(self, port: str, mac: str):
        self.ser = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=2)
        self.ser.port = port
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        # Force a visible warm boot so the identity is read from this connection,
        # even if the board was already running before the script opened the port.
        self.ser.reset_input_buffer()
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        self.ser.dtr = True
        self.mac = mac.upper()
        self.lines = []

    def read_until(self, predicate, timeout=35):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if line:
                print(line)
                self.lines.append(line)
                if predicate(line):
                    return line
        raise TimeoutError(f"{self.ser.port}: serial timeout")

    def ready(self):
        line = self.read_until(lambda s: s.startswith("READY,"))
        if f"mac={self.mac}" not in line or "partition=avtest" not in line:
            raise AssertionError(f"wrong board/partition: {line}")

    def command(self, command, prefix, timeout=45):
        self.ser.write((command + "\n").encode("ascii"))
        self.ser.flush()
        return self.read_until(lambda s: s.startswith(prefix), timeout)

    def reboot(self):
        self.ser.write(b"REBOOT\n")
        self.ser.flush()
        self.ready()

    def run(self):
        self.ready()
        test = self.command("MODULES_TEST", "MODULES_TEST,", timeout=90)
        if test != "MODULES_TEST,PASS,wal=2_objects,queue=ack+pending,history=2_objects,diag=retained2,budget=program_limit":
            raise AssertionError(f"module write campaign failed: {test}")
        self.reboot()
        recovery = self.command("MODULES_RECOVER", "MODULES_RECOVER,", timeout=45)
        if not recovery.startswith("MODULES_RECOVER,PASS,"):
            raise AssertionError(f"module recovery failed: {recovery}")
        match = re.search(r"wal=(\d+)/(\d+),queue_count=(\d+),history_objects=(\d+),diag_latest=(\d+)", recovery)
        if not match or tuple(map(int, match.groups())) != (11, 21, 1, 2, 3):
            raise AssertionError(f"unexpected recovered state: {recovery}")
        return {
            "port": self.ser.port,
            "mac": self.mac,
            "runs": 1,
            "warm_restarts": 1,
            "partition": "avraw",
            "modules_test": "PASS",
            "recovery": "PASS",
            "recovered_state": {"wal": [11, 21], "pending_queue_items": 1,
                                "history_objects": 2, "latest_diagnostic_event": 3},
            "physical_power_cut": "NOT_RUN",
            "exact_flash_program_interruption": "UNVERIFIED",
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--expect-mac", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    board = Board(args.port, args.expect_mac)
    try:
        result = board.run()
    finally:
        board.ser.close()
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
