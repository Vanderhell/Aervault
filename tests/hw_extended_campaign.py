"""ESP32-S3 campaign for the isolated raw NOR adapter, seal and MCU pairing.

The PC is only a serial message relay for the pair test; both endpoints perform
key derivation and authenticated message processing on their own MCU.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial


class Board:
    def __init__(self, port, mac):
        self.port, self.mac = port, mac.upper()
        self.ser = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
        self.ser.port = port
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.lines = []
        self.status = self.command("STATUS", "STATUS,")
        if f"mac={self.mac}" not in self.status:
            raise AssertionError(f"wrong device at {port}: {self.status}")

    def wait_line(self, predicate, timeout=15):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if not line:
                continue
            self.lines.append(line)
            print(f"{self.port},{line}", flush=True)
            if predicate(line):
                return line
        raise TimeoutError(f"{self.port}: response timeout; recent={self.lines[-12:]}")

    def command(self, text, prefix, timeout=20):
        self.ser.write((text + "\n").encode("ascii"))
        self.ser.flush()
        return self.wait_line(lambda x: x.startswith(prefix), timeout)

    def reboot(self):
        self.ser.write(b"REBOOT\n")
        self.ser.flush()
        return self.wait_line(lambda x: x.startswith("READY,"), 30)

    def close(self):
        self.ser.close()


def fields(line):
    return dict(part.split("=", 1) for part in line.split(",")[1:] if "=" in part)


def run(args):
    a = Board(args.com19, args.mac19)
    try:
        b = Board(args.com33, args.mac33)
    except Exception:
        a.close()
        raise
    try:
        result = {"boards": {}, "pair": {}, "transport": "host serial relay"}
        for board in (a, b):
            mac = board.mac
            nor = board.command("NOR_TEST", "NOR_TEST,", 40)
            if not nor.endswith("PASS"):
                raise AssertionError(f"{board.port}: {nor}")
            boot = board.reboot()
            nor_mount = board.command("NOR_MOUNT", "NOR_MOUNT,", 20)
            if not nor_mount.endswith("PASS"):
                raise AssertionError(f"{board.port}: {nor_mount}")

            provision = board.command("SEAL_PROVISION", "SEAL_PROVISION,")
            if "status=0" not in provision and "status=6" not in provision:
                raise AssertionError(f"{board.port}: {provision}")
            first = fields(board.command("SEAL_WRITE", "SEAL_WRITE,"))
            if first.get("status") != "0":
                raise AssertionError(f"{board.port}: seal write {first}")
            tamper = board.command("SEAL_TAMPER", "SEAL_TAMPER,")
            if not tamper.endswith("PASS_NO_PLAINTEXT"):
                raise AssertionError(f"{board.port}: {tamper}")
            board.reboot()
            read = fields(board.command("SEAL_READ", "SEAL_READ,"))
            if not board.lines[-1].startswith("SEAL_READ,PASS,") or read.get("nonce") != first.get("nonce"):
                raise AssertionError(f"{board.port}: seal recovery mismatch")
            second = fields(board.command("SEAL_WRITE", "SEAL_WRITE,"))
            if second.get("status") != "0" or second.get("nonce") == first.get("nonce"):
                raise AssertionError(f"{board.port}: nonce repeated after restart")
            metrics = fields(board.command("METRICS", "METRICS,"))
            result["boards"][mac] = {
                "port": board.port,
                "nor_test": "PASS",
                "nor_bank_recovery_after_warm_restart": "PASS",
                "seal_store_and_read_after_warm_restart": "PASS",
                "seal_nonce_before_restart": first.get("nonce"),
                "seal_nonce_after_restart": second.get("nonce"),
                "seal_nonce_unique": True,
                "tampered_seal_plaintext_blocked": True,
                "boot_after_nor": boot,
                "stack_metrics": metrics,
            }

        for mode in ("PSK", "X25519"):
            ka = fields(a.command(f"PAIR_KEYS {mode}", "PAIR_KEYS,"))
            kb = fields(b.command(f"PAIR_KEYS {mode}", "PAIR_KEYS,"))
            if ka.get("status") == "FAIL" or kb.get("status") == "FAIL":
                raise AssertionError(f"pair derive failed: {mode} {ka} {kb}")
            if ka.get("tx") != kb.get("rx") or ka.get("rx") != kb.get("tx"):
                raise AssertionError(f"directional keys do not match across boards: {mode}")
            for board in (a, b):
                reject = board.command(f"PAIR_BAD {mode}", "PAIR_BAD,")
                if not reject.endswith("PASS_REJECTED"):
                    raise AssertionError(f"foreign identity accepted on {board.port}: {reject}")
            result["pair"][mode] = {"directional_keys_match": True, "foreign_identity_rejected_on_both": True}

        packet = fields(a.command("PAIR_ENC", "PAIR_PACKET,"))
        positive = b.command("PAIR_DEC " + " ".join(packet[k] for k in ("nonce", "cipher", "tag")),
                             "PAIR_MESSAGE,")
        if not positive.endswith("PASS"):
            raise AssertionError(f"peer message rejected: {positive}")
        tampered_cipher = packet["cipher"]
        tampered_cipher = ("00" if tampered_cipher[:2] != "00" else "01") + tampered_cipher[2:]
        rejected = b.command("PAIR_DEC " + " ".join((packet["nonce"], tampered_cipher, packet["tag"])),
                             "PAIR_MESSAGE,")
        if not rejected.endswith("TAMPER_REJECTED_NO_PLAINTEXT"):
            raise AssertionError(f"tampered peer message accepted: {rejected}")
        result["pair"]["message_relay"] = {"valid_message": "PASS", "tampered_message": "REJECTED_NO_PLAINTEXT"}
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2), flush=True)
    finally:
        a.close(); b.close()


def run_single(port, mac, output):
    board = Board(port, mac)
    try:
        nor = board.command("NOR_TEST", "NOR_TEST,", 40)
        if not nor.endswith("PASS"):
            raise AssertionError(f"{board.port}: {nor}")
        board.reboot()
        nor_mount = board.command("NOR_MOUNT", "NOR_MOUNT,", 20)
        if not nor_mount.endswith("PASS"):
            raise AssertionError(f"{board.port}: {nor_mount}")
        provision = board.command("SEAL_PROVISION", "SEAL_PROVISION,")
        if "status=0" not in provision and "status=6" not in provision:
            raise AssertionError(f"{board.port}: {provision}")
        first = fields(board.command("SEAL_WRITE", "SEAL_WRITE,"))
        if first.get("status") != "0":
            raise AssertionError(f"{board.port}: seal write {first}")
        if not board.command("SEAL_TAMPER", "SEAL_TAMPER,").endswith("PASS_NO_PLAINTEXT"):
            raise AssertionError(f"{board.port}: tampered seal plaintext check failed")
        board.reboot()
        read_line = board.command("SEAL_READ", "SEAL_READ,")
        read = fields(read_line)
        if not read_line.startswith("SEAL_READ,PASS,") or read.get("nonce") != first.get("nonce"):
            raise AssertionError(f"{board.port}: seal recovery mismatch")
        second = fields(board.command("SEAL_WRITE", "SEAL_WRITE,"))
        if second.get("status") != "0" or second.get("nonce") == first.get("nonce"):
            raise AssertionError(f"{board.port}: nonce repeated after restart")
        metrics = fields(board.command("METRICS", "METRICS,"))
        result = {"port": port, "mac": mac.upper(), "runs": 1,
                  "nor_test": "PASS", "nor_bank_recovery_after_warm_restart": "PASS",
                  "seal_store_and_read_after_warm_restart": "PASS",
                  "tampered_seal_plaintext_blocked": True,
                  "nonces": [first.get("nonce"), second.get("nonce")],
                  "nonce_unique": True, "stack_metrics": metrics,
                  "pairing": "NOT RUN: the peer board is not part of single-board mode"}
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2), flush=True)
    finally:
        board.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--com19", default="COM19")
    p.add_argument("--com33", default="COM33")
    p.add_argument("--mac19", default="3C:0F:02:DE:77:44")
    p.add_argument("--mac33", default="3C:0F:02:DE:77:FC")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--single-port", default=None)
    p.add_argument("--single-mac", default=None)
    args = p.parse_args()
    if args.single_port:
        if not args.single_mac:
            p.error("--single-port requires --single-mac")
        run_single(args.single_port, args.single_mac, args.output)
    else:
        run(args)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
