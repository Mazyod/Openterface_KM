param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$tempDir = 'C:\tmp'

$buildDir = @('build', 'obj') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path (Join-Path $_ 'makefile') } |
    Select-Object -First 1

if (-not $buildDir) {
    throw "No generated firmware makefile found under '$repoRoot\\build' or '$repoRoot\\obj'."
}

$toolchainPaths = @(
    'C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin',
    'C:\Qt\Tools\mingw1120_64\bin',
    'C:\msys64\usr\bin'
)

foreach ($pathEntry in $toolchainPaths) {
    if (-not (Test-Path $pathEntry)) {
        throw "Required tool path not found: $pathEntry"
    }
}

New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$env:PATH = ($toolchainPaths -join ';') + ';' + $env:PATH
$env:TEMP = $tempDir
$env:TMP = $tempDir

Push-Location $buildDir
try {
    if ($Clean) {
        & mingw32-make -f makefile clean
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    & mingw32-make -f makefile -j1 main-build
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}