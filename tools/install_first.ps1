param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$RomImage,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$full = Join-Path $build 'FoloToy-AI-Passport-full.bin'
$saves = Join-Path $build 'saves.bin'
$table = Join-Path $build 'partition_table\partition-table.bin'
foreach ($path in @($full, $saves, $table)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Missing build artifact: ' + $path) }
}
if (-not $env:IDF_PATH) { throw 'Activate ESP-IDF 5.5.3 first.' }
$python = (Get-Command python.exe -ErrorAction Stop).Source
& $python (Join-Path $PSScriptRoot 'verify_firmware.py') $build
if ($LASTEXITCODE -ne 0) { throw 'Merged firmware validation failed.' }
& $python (Join-Path $PSScriptRoot 'pack_roms.py') --verify $RomImage
if ($LASTEXITCODE -ne 0) { throw 'ROM image validation failed.' }
$generator = Join-Path $env:IDF_PATH 'components\partition_table\gen_esp32part.py'
$rows = & $python $generator $table
if ($LASTEXITCODE -ne 0) { throw 'Partition table validation failed.' }
$saveRow = $rows | Where-Object { $_ -match '^saves,data,spiffs,' } | Select-Object -First 1
if (-not $saveRow) { throw 'No saves partition in built table.' }
$fields = $saveRow -split ','
$saveOffset = [Convert]::ToUInt32($fields[3].Substring(2), 16)
$saveSizeText = $fields[4]
if ($saveSizeText -notmatch '^([0-9]+)([KMG]?)$') { throw 'Invalid saves partition size.' }
$saveSize = [uint64]$Matches[1]
switch ($Matches[2]) { 'K' { $saveSize *= 1024 } 'M' { $saveSize *= 1048576 } 'G' { $saveSize *= 1073741824 } }
if ((Get-Item -LiteralPath $saves).Length -ne $saveSize) { throw 'Initial saves image size mismatch.' }
$romPath = (Resolve-Path -LiteralPath $RomImage).Path
Write-Output ('Device: ' + $Port)
Write-Output ('Firmware: 0x0 ' + $full)
Write-Output ('ROMs: ' + $romPath)
Write-Output ('Initial saves: 0x{0:X} {1}' -f $saveOffset, $saves)
Write-Output 'This replaces the installed application and partition table and resets NVS, ROMs, and saves.'
if (-not $Execute) {
    Write-Output 'Dry run only. Pass -Execute after reviewing this output.'
    return
}
& $python -m esptool --chip esp32c3 -p $Port --before default_reset --after hard_reset write_flash 0x0 $full ('0x{0:X}' -f $saveOffset) $saves
if ($LASTEXITCODE -ne 0) { throw 'Firmware or initial saves write failed.' }
$firmwareReadback = Join-Path $build 'firmware_readback.bin'
& $python -m esptool --chip esp32c3 -p $Port --before default_reset --after hard_reset read_flash 0x0 (Get-Item -LiteralPath $full).Length $firmwareReadback
if ($LASTEXITCODE -ne 0) { throw 'Firmware readback failed.' }
& $python (Join-Path $PSScriptRoot 'verify_firmware.py') $build $firmwareReadback
if ($LASTEXITCODE -ne 0) { throw 'Firmware image readback mismatch.' }
$readback = Join-Path $build 'saves_readback.bin'
& $python -m esptool --chip esp32c3 -p $Port --before default_reset --after hard_reset read_flash ('0x{0:X}' -f $saveOffset) $saveSize $readback
if ($LASTEXITCODE -ne 0) { throw 'Initial saves readback failed.' }
if ((Get-FileHash -LiteralPath $readback -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $saves -Algorithm SHA256).Hash) { throw 'Initial saves readback hash mismatch.' }
& (Join-Path $PSScriptRoot 'import_roms.ps1') -Port $Port -Image $romPath -Execute
if (-not $?) { throw 'ROM import failed.' }
Write-Output 'First-install images verified by readback. Proceed to on-device behavior tests.'
