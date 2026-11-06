param(
    [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.1',
    [string]$ToolsPath = 'C:\Espressif',
    [string]$Python = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectRoot = Join-Path $root 'examples\esp32s3_crypto'
$buildRoot = Join-Path $projectRoot 'build-crypto-size-v4'
$microcrypt = Join-Path $root 'build\deps-microcrypt'
$microdh = Join-Path $root 'build\deps-microdh'
$sizeTool = Get-ChildItem (Join-Path $ToolsPath 'tools\xtensa-esp-elf') -Recurse -Filter 'xtensa-esp32s3-elf-size.exe' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $sizeTool) { throw 'ESP32-S3 xtensa size tool not found.' }
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
$env:PATH = "$(Split-Path $Python);$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF environment setup failed (exit $LASTEXITCODE)" }
if (Test-Path $microcrypt) { $env:AV_MICROCRYPT_SOURCE_DIR = $microcrypt } else { Remove-Item Env:AV_MICROCRYPT_SOURCE_DIR -ErrorAction SilentlyContinue }
if (Test-Path $microdh) { $env:AV_MICRODH_SOURCE_DIR = $microdh } else { Remove-Item Env:AV_MICRODH_SOURCE_DIR -ErrorAction SilentlyContinue }
$variants = @(
    @{ name='core'; config=@() },
    @{ name='auth'; config=@('CONFIG_AV_FOOTPRINT_AUTH=y') },
    @{ name='seal'; config=@('CONFIG_AV_FOOTPRINT_SEAL=y') },
    @{ name='pair'; config=@('CONFIG_AV_FOOTPRINT_PAIR=y') }
)
$rows = @()
foreach ($variant in $variants) {
    $name = $variant.name
    $project = Join-Path $projectRoot "variants\$name"
    $build = Join-Path $buildRoot $name
    New-Item -ItemType Directory -Path $build -Force | Out-Null
    $defaults = @('CONFIG_IDF_TARGET="esp32s3"','CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y') + $variant.config
    Set-Content -LiteralPath (Join-Path $build 'sdkconfig.defaults') -Value $defaults -Encoding ascii
    & $Python (Join-Path $IdfPath 'tools\idf.py') -C $project -B $build --no-ccache -D "SDKCONFIG=$(Join-Path $build 'sdkconfig')" build
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed for $name (exit $LASTEXITCODE)" }
    $elf = Join-Path $build 'aervault_crypto_footprint.elf'
    $sizeOutput = & $sizeTool $elf
    if ($LASTEXITCODE -ne 0) { throw "size failed for $name" }
    $numeric = $sizeOutput | Where-Object { $_ -match '^\s*\d+\s+\d+\s+\d+\s+\d+\s+[0-9a-fA-F]+\s+' } | Select-Object -Last 1
    if (-not $numeric) { throw "Cannot parse size output for ${name}: $sizeOutput" }
    $cols = ($numeric -split '\s+') | Where-Object { $_ -ne '' }
    $text = [int64]$cols[0]; $data = [int64]$cols[1]; $bss = [int64]$cols[2]
    $rows += [pscustomobject]@{ variant=$name; elf="build-crypto-size-v4/$name/aervault_crypto_footprint.elf"; text_bytes=$text; data_bytes=$data; bss_bytes=$bss; flash_bytes=($text+$data); static_ram_bytes=($data+$bss) }
}
$baseline = $rows[0]
foreach ($row in $rows) {
    $row | Add-Member -NotePropertyName flash_delta -NotePropertyValue ($row.flash_bytes-$baseline.flash_bytes)
    $row | Add-Member -NotePropertyName static_ram_delta -NotePropertyValue ($row.static_ram_bytes-$baseline.static_ram_bytes)
}
$report = Join-Path $projectRoot 'crypto-size-results.json'
$rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding utf8
$rows | Format-Table variant,flash_bytes,flash_delta,static_ram_bytes,static_ram_delta -AutoSize
Write-Output "Wrote $report"
