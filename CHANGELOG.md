# Changelog

## 0.1.0 — 2026-11-06

Initial versioned preview release.

- Added separately linkable C99 core and monotonic counter targets with the
  caller-owned API and AV01 record compatibility.
- Added opt-in authentication, AES-GCM seal, PSK/X25519 pairing, ESP-IDF NVS
  and NOR adapters.
- Added the shared two-bank journal, WAL, durable queue, bounded history,
  diagnostics, and write budget modules.
- Added host fault injection, crypto vectors, storage campaigns, sanitizer
  builds, CMake package export, and ESP32-S3 test firmware.
- Host validation: 14/14 CTest cases passed normally, including installed
  package consumers; 13/13 functional/link tests passed under ASan/UBSan.
- ESP32-S3 COM19 and COM33 module campaigns passed. A probe-confirmed cut at
  the exact flash-program interval remains unverified.

This is a pre-1.0 release. Treat the API as preview quality; storage formats
and optional module APIs may evolve before 1.0.
