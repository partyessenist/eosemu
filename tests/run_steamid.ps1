# Builds and runs the EOSEmu Steam external-account id test.
#
# Verifies Platform::LocalSteamId's resolution order end to end through the
# public Connect API, in three scenarios sharing one harness (steamid.c):
#   1. ticket only              -> id parsed from the Steam session ticket
#   2. ticket + [Identity] SteamId config -> the configured id wins
#   3. ticket + fake steam_api64.dll      -> the live module query wins
# The fake module (fake_steam_api.c) exports the three flat-API functions
# EOSEmu probes, exactly like real Steam / a Steam emulator would.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot          # EOSEmu/
$repo = Split-Path -Parent $root                  # repo root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_steamid"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $dll)) { throw "Build the DLL first: cmake --build build --config Debug" }
Copy-Item $dll $outDir -Force
# A stray config would change the resolution order under test.
Remove-Item (Join-Path $outDir "eosemu.ini") -ErrorAction SilentlyContinue
Remove-Item Env:\EOSEMU_CONFIG -ErrorAction SilentlyContinue

# The three ids the scenarios expect. Ticket/config ids are arbitrary valid
# public-individual SteamID64s; the module id must match fake_steam_api.c.
$ticketId = 76561197960287930
$configId = 76561198000000001
$moduleId = 76561197971234567

# Craft a Steam auth session ticket embedding $ticketId, hex-encoded the way a
# game passes it to EOS: GC token section (u32 len=20, u64 token, u64 steamid,
# u32 gentime) followed by a dummy session-header tail.
$bytes = [System.Collections.Generic.List[byte]]::new()
$bytes.AddRange([BitConverter]::GetBytes([uint32]20))
$bytes.AddRange([BitConverter]::GetBytes([uint64]0x0102030405060708))
$bytes.AddRange([BitConverter]::GetBytes([uint64]$ticketId))
$bytes.AddRange([BitConverter]::GetBytes([uint32]1700000000))
$bytes.AddRange([BitConverter]::GetBytes([uint32]24))
$bytes.AddRange([byte[]]::new(20))
$ticketHex = -join ($bytes | ForEach-Object { $_.ToString("X2") })

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\steamid.c"
$fakeSrc = Join-Path $root "tests\fake_steam_api.c"
$exe = Join-Path $outDir "steamid.exe"
$fakeDll = Join-Path $outDir "steam_api64.dll"

$prevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`"" +
    " && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$outDir\steamid.obj`" /link `"$sdkLib`"" +
    " && cl /nologo /W3 /LD `"$fakeSrc`" /Fe:`"$fakeDll`" /Fo:`"$outDir\fake_steam_api.obj`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed (no exe produced)" }
if (-not (Test-Path $fakeDll)) { throw "compile failed (no fake steam_api64.dll produced)" }

$fail = 0

Write-Host "--- scenario 1: session ticket (expect $ticketId) ---"
& $exe "$ticketId" $ticketHex 0
if ($LASTEXITCODE -ne 0) { $fail = 1 }

Write-Host "--- scenario 2: [Identity] SteamId pin beats the ticket (expect $configId) ---"
$iniPath = Join-Path $outDir "steamid_pin.ini"
Set-Content -Path $iniPath -Encoding utf8 -Value @"
[Identity]
SteamId = $configId
"@
$env:EOSEMU_CONFIG = $iniPath
& $exe "$configId" $ticketHex 0
if ($LASTEXITCODE -ne 0) { $fail = 1 }
Remove-Item Env:\EOSEMU_CONFIG -ErrorAction SilentlyContinue

Write-Host "--- scenario 3: loaded steam_api module beats the ticket (expect $moduleId) ---"
& $exe "$moduleId" $ticketHex 1
if ($LASTEXITCODE -ne 0) { $fail = 1 }

Write-Host "--- overall: $(if ($fail -eq 0) { 'PASS' } else { 'FAIL' }) ---"
exit $fail
