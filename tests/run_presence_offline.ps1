# Builds tests/presence_offline.c and runs a watcher plus two sequential goer
# instances (same profile) on this machine, validating that a peer which leaves
# the LAN flips to Offline while staying a friend, then returns Online when it
# comes back. See tests/presence_offline.c for the assertions.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_liveness"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\presence_offline.c"
$exe = Join-Path $outDir "presence_offline.exe"
$obj = Join-Path $outDir "presence_offline.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

Write-Host "--- launching watcher + two goers ---"
$watchOut = Join-Path $outDir "watcher.txt"
$goer1Out = Join-Path $outDir "goer1.txt"
$goer2Out = Join-Path $outDir "goer2.txt"

# Start-Process (WinPS 5.1) has no -Environment; a child inherits the parent env
# at launch, so set EOSEMU_PROFILE just before each launch.
$env:EOSEMU_PROFILE = "liveW"
$watchProc = Start-Process -FilePath $exe -ArgumentList "watcher" -WorkingDirectory $outDir `
	-RedirectStandardOutput $watchOut -PassThru -NoNewWindow
Start-Sleep -Milliseconds 1000

# Goer run #1 -- announces, then cleanly exits (broadcasts Goodbye).
$env:EOSEMU_PROFILE = "liveG"
$goer1 = Start-Process -FilePath $exe -ArgumentList "goer" -WorkingDirectory $outDir `
	-RedirectStandardOutput $goer1Out -PassThru -NoNewWindow
$goer1.WaitForExit(20000) | Out-Null
if (-not $goer1.HasExited) { $goer1.Kill() }

# Let the watcher observe the offline transition before the peer returns.
Start-Sleep -Seconds 4

# Goer run #2 -- SAME profile => same Epic account id returning online.
$env:EOSEMU_PROFILE = "liveG"
$goer2 = Start-Process -FilePath $exe -ArgumentList "goer" -WorkingDirectory $outDir `
	-RedirectStandardOutput $goer2Out -PassThru -NoNewWindow
Remove-Item Env:\EOSEMU_PROFILE
$goer2.WaitForExit(20000) | Out-Null
if (-not $goer2.HasExited) { $goer2.Kill() }

$watchProc.WaitForExit(55000) | Out-Null
if (-not $watchProc.HasExited) { $watchProc.Kill() }

Write-Host "===== GOER #1 ====="; Get-Content $goer1Out | Out-Host
Write-Host "===== GOER #2 ====="; Get-Content $goer2Out | Out-Host
Write-Host "===== WATCHER ====="; Get-Content $watchOut | Out-Host

if (Select-String -Path $watchOut -Pattern "LIVENESS-WATCH PASS" -Quiet) {
	Write-Host "LIVENESS ACCEPTANCE: PASS"; exit 0
}
Write-Host "LIVENESS ACCEPTANCE: FAIL"; exit 1
