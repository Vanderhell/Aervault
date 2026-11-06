# ESP32-S3 storage and crypto test firmware

Test-only ESP-IDF 5.5.1 application for the NVS backend, NOR adapter, crypto
adapters and journal modules. It uses the isolated `avtest` NVS partition and
`avraw` raw flash partition. **Campaign commands erase or format test data in
these partitions. Do not use this partition table on devices with valuable
data.**

From this directory, with ESP-IDF exported:

```powershell
idf.py build
idf.py -p COM19 app-flash
```

The default CMake components fetch microflash, microcrypt and microdh from the
pinned commits in their component files. Set `AV_MICROFLASH_SOURCE_DIR`,
`AV_MICROCRYPT_SOURCE_DIR` or `AV_MICRODH_SOURCE_DIR` to existing checkouts at
those exact commits to build offline.

From the repository root, run the focused hardware campaign for WAL, queue,
history, diagnostics and budget:

```powershell
python tests/hw_modules_campaign.py --port COM19 `
  --expect-mac 3C:0F:02:DE:77:44 `
  --output examples/esp32s3_nvs/COM19-modules-results.json
```

The runner formats only `avraw`, checks the reported board MAC, executes the
module writes, performs one warm restart and checks recovered state. Results
for the tested COM19 and COM33 boards are saved beside this README. This does
not test a physical power cut. The exact instant of interruption during flash
programming remains `UNVERIFIED` without external instrumentation.

Other focused runners are in `tests/`. They validate the MAC before issuing
commands. Physical power-cut testing requires a person to disconnect power at
the prompted time; it is separate from warm-reset campaigns.
