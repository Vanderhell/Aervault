# Aervault

Aervault is a C99 library for small persistent records on MCUs. The core uses
caller-owned RAM and performs no dynamic allocation. AV01 records and the
original object API remain supported; the convenience API configures a backend
and object rules once with `av_open()`, then uses object IDs with `av_put()` and
`av_get()`.

## Modules

| CMake target | Purpose | Dependencies |
| --- | --- | --- |
| `aervault::core` | AV01 records, authentication callback, recovery and writes | none |
| `aervault::counter` | Durable monotonic range reservation | `aervault::core` |
| `aervault::auth` | HMAC-SHA256 adapter | microcrypt |
| `aervault::seal` | AES-GCM storage envelope and durable nonce reservation | counter, microcrypt |
| `aervault::pair` | PSK-based MCU peer key derivation | microcrypt |
| `aervault::pair` with `AV_PAIR_X25519=ON` | Identity-checked X25519 peer derivation | microcrypt, microdh |
| `aervault::nvs` | ESP-IDF NVS backend | ESP-IDF |
| `aervault::nor` | NOR partition adapter | microflash |
| `aervault::journal` | Two-bank durable append journal | none |
| `aervault::wal`, `aervault::queue`, `aervault::history`, `aervault::diag` | Optional journal clients | journal; history also core |
| `aervault::budget` | Program/erase admission limits | optional NOR counter adapter |

Targets are selected independently. Core-only applications do not link counter,
storage, or crypto implementations. Crypto dependencies are fetched at pinned
revisions only when their targets are enabled.

## Host build and tests

Core and counter regressions:

```sh
cmake -S . -B build -DAV_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Enable all optional host adapters and storage modules:

```sh
cmake -S . -B build-full -DAV_TESTS=ON \
  -DAV_ENABLE_AUTH=ON -DAV_ENABLE_SEAL=ON -DAV_ENABLE_PAIR=ON \
  -DAV_PAIR_X25519=ON -DAV_ENABLE_NOR=ON -DAV_ENABLE_JOURNAL=ON \
  -DAV_ENABLE_WAL=ON -DAV_ENABLE_QUEUE=ON -DAV_ENABLE_HISTORY=ON \
  -DAV_ENABLE_BUDGET=ON -DAV_ENABLE_DIAG=ON
cmake --build build-full
ctest --test-dir build-full --output-on-failure
```

This suite includes known crypto vectors, storage fault regressions, a seeded
10,000-run host campaign, and linker checks. Run the same CTest suite in
separate ASan and UBSan build directories for sanitizer coverage. The
fault-injection RAM/NOR simulators are host test backends, not evidence of
physical power-loss timing.

## ESP32-S3 validation

`examples/esp32s3_nvs` builds an ESP-IDF test firmware with isolated `avtest`
NVS and `avraw` NOR partitions. It provides serial campaigns for NVS, NOR,
crypto and journal modules. Per-board results are in
`examples/esp32s3_nvs/*-results.json`. Current recorded results cover COM19 and
COM33, including WAL, queue, history, diagnostics, and budget recovery through
`avraw`. A probe-confirmed interruption during the exact flash-program
interval remains unverified.

Build with ESP-IDF 5.5.1 from `examples/esp32s3_nvs`:

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.1\export.ps1
idf.py build
```

## Safety notes

`AV_STRICT` blocks access if the authenticated redundant state is missing or
damaged. Security object provisioning is explicit. Counter ranges are returned
only after durable commit is confirmed; uncertain commits return no value.
Applications must provide a secure key store and a backend whose success
contract means durable storage. The test firmware's tag callback and identities
are test-only.

## License

Copyright 2026 Vanderhell. Aervault is licensed under the Apache License 2.0;
see [LICENSE](LICENSE). Third-party mechanism and dependency notices are in
[NOTICE](NOTICE) and `docs/storage-mechanisms.md`.
