#!/usr/bin/env python3
"""Inspect defined symbols in component-only smoke binaries."""
import argparse
import subprocess


def symbols(nm, path):
    text = subprocess.check_output([nm, "--defined-only", path], text=True)
    return {line.split()[-1] for line in text.splitlines() if line.split()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--nm", required=True)
    for name in ("core", "auth", "seal", "pair"):
        p.add_argument(f"--{name}", required=True)
    p.add_argument("--pair-psk")
    a = p.parse_args()
    bins = {name: symbols(a.nm, getattr(a, name)) for name in ("core", "auth", "seal", "pair")}
    checks = {
        "core": ("av_open",),
        "auth": ("av_auth_tag", "mcrypt_hmac_sha256"),
        "seal": ("av_seal_encrypt", "av_counter_next", "mcrypt_aes128_gcm_encrypt"),
        "pair": ("av_pair_x25519", "mdh_shared_secret_checked", "mcrypt_hmac_sha256"),
    }
    forbidden = {
        "core": ("av_counter_next", "mcrypt_", "mdh_", "av_nvs_", "nvs_"),
        "auth": ("av_counter_next", "mdh_", "av_nvs_", "nvs_"),
        "seal": ("mdh_", "av_nvs_", "nvs_"),
        "pair": ("av_counter_next", "av_nvs_", "nvs_"),
    }
    for name, required in checks.items():
        for symbol in required:
            assert symbol in bins[name], f"{name} missing {symbol}"
        for prefix in forbidden[name]:
            assert not any(s.startswith(prefix) for s in bins[name]), f"{name} unexpectedly contains {prefix}"
    if a.pair_psk:
        psk = symbols(a.nm, a.pair_psk)
        assert "av_pair_derive" in psk and "mcrypt_hmac_sha256" in psk
        assert not any(s.startswith("mdh_") or s.startswith("av_counter_next") for s in psk)
    print("link separation: core / auth / seal-no-microdh / pair-with-microdh PASS")


if __name__ == "__main__":
    main()
