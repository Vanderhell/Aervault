# ESP32-S3 crypto footprint

This non-flashed IDF firmware emits a separate ELF for core, core+auth,
core+seal+counter, and core+pair+X25519. It uses identical IDF/sdkconfig and
compiler settings per build directory. The project sources the pinned upstream
libraries unchanged through the IDF adapter components. `AV_MICROCRYPT_SOURCE_DIR`
and `AV_MICRODH_SOURCE_DIR` may point at checkouts at the locked revisions to
avoid re-downloading them.

Use ESP-IDF 5.5.1 and the repository's `tests/esp32s3_crypto_size.ps1` runner.
The runner records `.text`, `.data`, `.bss`, total flash load size and deltas
from the core ELF. It does not flash or contact hardware.
