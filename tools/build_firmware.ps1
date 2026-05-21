param(
    [switch]$Clean,
    [ValidateSet('hid_acm', 'hid_uac_acm', 'hid_acm_bridge', 'hid_ecm_bridge')]
    [string]$UsbWorkMode = 'hid_uac_acm'
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# Build output directory
$buildDir = Join-Path $repoRoot "firmware_build"

# Copy the built firmware to obj/ for MounRiver Studio compatibility
$objDir = Join-Path $repoRoot "obj"
if (Test-Path $objDir) {
    $binFile = Join-Path $buildDir "openterface-km-firmware.bin"
    $hexFile = Join-Path $buildDir "openterface-km-firmware.hex"
    $elfFile = Join-Path $buildDir "openterface-km-firmware.elf"

    if (Test-Path $binFile) {
        Copy-Item $binFile (Join-Path $objDir "openterface-km-firmware.bin") -Force
        Write-Host "Copied firmware binary to obj/"
    }
}

# Run the standalone build
$buildScript = Join-Path $scriptDir "build_firmware.sh"
if (-not (Test-Path $buildScript)) {
    throw "Build script not found: $buildScript"
}

Write-Host "Building firmware..."
if ($Clean) {
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force
    }
}

& bash $buildScript
exit $LASTEXITCODE
