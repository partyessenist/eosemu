# Builds and runs the EOSEmu overlay / EOS_UI lifecycle test.
# Compiles tests/overlay.c against the SDK import library, drops the freshly built
# EOSEmu DLL beside the exe, and runs it. The test briefly shows/hides the overlay
# window; it asserts the EOS_UI logical surface, not pixels.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot          # EOSEmu/
$repo = Split-Path -Parent $root                  # repo root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_overlay"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $dll)) { throw "Build the DLL first: cmake --build build --config Debug" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$src = Join-Path $root "tests\overlay.c"
$exe = Join-Path $outDir "overlay.exe"
$obj = Join-Path $outDir "overlay.obj"
$installerDir = Split-Path $vswhere -Parent

$prevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed (no exe produced)" }

Write-Host "--- running overlay test ---"
& $exe
$code = $LASTEXITCODE
Write-Host "--- exit code $code ---"
exit $code
