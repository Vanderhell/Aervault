# ESP32-S3 NVS adapter test

Build with ESP-IDF 5.5.1 from this directory:

```powershell
. C:\Espressif\frameworks\esp-idf-v5.5.1\export.ps1
idf.py build
```

The `aervault_core`, `aervault_counter`, and `aervault_nvs` components are
separate. NVS namespaces are exclusive to one AV object. The firmware uses a
test-only tag; it is not cryptographic and must not protect product data.

`partitions.csv` preserves COM19's original partition offsets and sizes and
adds a factory test app plus isolated 128 KiB `avtest` NVS. COM19's original
boot metadata and unused flash range are saved under `build/` before staging
and restored after the physical test. COM33 requires the user-authorized full
erase before this 16 MiB test layout is installed.

Run the warm-reset campaign from the repository root; the harness validates
the MAC and checks all emitted counter ranges for overlap:

```powershell
python tests/hw_campaign.py --port COM19 `
  --expect-mac 3C:0F:02:DE:77:44 --output examples/esp32s3_nvs/COM19-results.json
```

For the physical power-cut phase, erase only `avtest`, flash the updated app,
then run `tests/hw_power_cut.py` from the repository root. While the script
prints `UNPLUG_NOW`, physically remove board power during the active batch.
Reconnect the same cable without resetting or reflashing. The script verifies
USB disconnect, power-on reset reason, recovered strict state, and the next
counter range. It cannot independently prove the exact flash-program interval;
that field is reported `UNVERIFIED` without an external electrical probe.
