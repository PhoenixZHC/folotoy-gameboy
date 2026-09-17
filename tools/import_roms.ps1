param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Image,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$table = Join-Path $projectRoot 'build\partition_table\partition-table.bin'
$packer = Join-Path $PSScriptRoot 'pack_roms.py'
if (-not (Test-Path -LiteralPath $table -PathType Leaf)) { throw 'Build the firmware first; partition table is missing.' }
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw 'ROM image is missing.' }
if (-not $env:IDF_PATH) { throw 'Activate the ESP-IDF 5.5.3 environment first.' }
$python = (Get-Command python.exe -ErrorAction Stop).Source
$resolvedImage = (Resolve-Path -LiteralPath $Image).Path

& $python $packer --verify $resolvedImage
if ($LASTEXITCODE -ne 0) { throw 'ROM image verification failed.' }
$generator = Join-Path $env:IDF_PATH 'components\partition_table\gen_esp32part.py'
$rows = & $python $generator $table
if ($LASTEXITCODE -ne 0) { throw 'Partition table verification failed.' }
$line = $rows | Where-Object { $_ -match '^roms,data,64,' } | Select-Object -First 1
if (-not $line) { throw 'Built partition table has no ROM partition.' }
$fields = $line -split ','
$offset = [Convert]::ToUInt32($fields[3].Substring(2), 16)
$sizeText = $fields[4]
if ($sizeText -notmatch '^([0-9]+)([KMG]?)$') { throw 'Unrecognized ROM partition size.' }
$size = [uint64]$Matches[1]
switch ($Matches[2]) { 'K' { $size *= 1024 } 'M' { $size *= 1048576 } 'G' { $size *= 1073741824 } }
$imageInfo = Get-Item -LiteralPath $resolvedImage
if ($imageInfo.Length -gt $size) { throw 'ROM image exceeds the built ROM partition.' }
$digest = (Get-FileHash -LiteralPath $resolvedImage -Algorithm SHA256).Hash
Write-Output ('Device: ' + $Port)
Write-Output ('ROM partition: 0x{0:X}, {1} bytes' -f $offset, $size)
Write-Output ('Image: {0}, {1} bytes' -f $resolvedImage, $imageInfo.Length)
Write-Output ('SHA-256: ' + $digest)
Write-Output 'Only the ROM partition will be written; existing ROMs there will be replaced.'
if (-not $Execute) {
    Write-Output 'Dry run only. Pass -Execute after reviewing this output.'
    return
}

& $python -m esptool --chip esp32c3 -p $Port --before default_reset --after hard_reset write_flash ('0x{0:X}' -f $offset) $resolvedImage
if ($LASTEXITCODE -ne 0) { throw 'ROM partition write failed.' }
$readback = Join-Path $projectRoot 'build\roms_readback.bin'
& $python -m esptool --chip esp32c3 -p $Port --before default_reset --after hard_reset read_flash ('0x{0:X}' -f $offset) $imageInfo.Length $readback
if ($LASTEXITCODE -ne 0) { throw 'ROM partition readback failed.' }
$readbackDigest = (Get-FileHash -LiteralPath $readback -Algorithm SHA256).Hash
if ($readbackDigest -ne $digest) { throw ('Readback hash mismatch; saved at ' + $readback) }
Write-Output ('ROM partition verified by readback. File: ' + $readback)
