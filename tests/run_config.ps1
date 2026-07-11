# Builds and runs the EOSEmu config test.
# Writes an eosemu.ini, points %EOSEMU_CONFIG% at it, and runs config.exe against
# the freshly built EOSEmu DLL. The DLL reads the config; the test asserts the
# configured DisplayName / Language / ownership / entitlement surface through the
# public API. The expected values here must match tests/config.c.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot          # EOSEmu/
$repo = Split-Path -Parent $root                  # repo root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_config"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $dll)) { throw "Build the DLL first: cmake --build build --config Debug" }
Copy-Item $dll $outDir -Force

# EOSEmu's own log sink writes here regardless of the game's callback.
$log = Join-Path $outDir "emu.log"
Remove-Item $log -ErrorAction SilentlyContinue

# The config the test asserts against.
$ini = @"
# EOSEmu config test fixture
[Logging]
File = $log
Level = Verbose

[Identity]
DisplayName = ConfiguredHero
Language = fr

[Ecom]
Entitlement = season_pass:cat_item_001:ent_season
OwnedItem = cat_item_001

[Stats]
kills = 7

[Achievements]
Unlocked = ach_first_win

[Sanctions]
Sanctioned = true
Action = RESTRICT_GAME_ACCESS

[Network]
DiscoveryPort = 47105
"@
$iniPath = Join-Path $outDir "eosemu.ini"
Set-Content -Path $iniPath -Value $ini -Encoding utf8

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$src = Join-Path $root "tests\config.c"
$exe = Join-Path $outDir "config.exe"
$obj = Join-Path $outDir "config.obj"
$installerDir = Split-Path $vswhere -Parent

$prevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed (no exe produced)" }

# The DLL reads %EOSEMU_CONFIG% at platform creation; the child inherits it.
$env:EOSEMU_CONFIG = $iniPath
Write-Host "--- running config test (EOSEMU_CONFIG=$iniPath) ---"
& $exe
$code = $LASTEXITCODE
Remove-Item Env:\EOSEMU_CONFIG -ErrorAction SilentlyContinue

# Verify EOSEmu's own log sink wrote to the configured file.
$logOk = $false
if (Test-Path $log) {
    $content = Get-Content $log -Raw
    if ($content -match 'LogEOS') { $logOk = $true }
    Write-Host "--- emu.log ($((Get-Content $log).Count) lines, sink=$logOk) ---"
    Get-Content $log | Select-Object -First 6 | ForEach-Object { Write-Host "   $_" }
} else {
    Write-Host "--- emu.log MISSING ---"
}

Write-Host "--- exit code $code ---"
if ($code -eq 0 -and -not $logOk) { Write-Host "LOG SINK FAIL"; exit 4 }
exit $code
