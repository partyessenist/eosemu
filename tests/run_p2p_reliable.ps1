# Builds tests/p2p_reliable.c and runs host + join with induced packet loss
# (EOSEMU_P2P_TESTLOSS, per-mille) to prove ReliableOrdered P2P delivers every
# packet exactly once, in order, despite ~30% drops on the data+ack path.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_rel"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\p2p_reliable.c"
$exe = Join-Path $outDir "p2p_reliable.exe"
$obj = Join-Path $outDir "p2p_reliable.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

Write-Host "--- launching host + join with 30% induced packet loss ---"
$hostOut = Join-Path $outDir "host.txt"
$joinOut = Join-Path $outDir "join.txt"

# 300 per-mille = 30% of data+ack datagrams dropped. Both processes inherit it.
$env:EOSEMU_P2P_TESTLOSS = "300"

$env:EOSEMU_PROFILE = "relHost"
$hostProc = Start-Process -FilePath $exe -ArgumentList "host" -WorkingDirectory $outDir `
	-RedirectStandardOutput $hostOut -PassThru -NoNewWindow
Start-Sleep -Milliseconds 800
$env:EOSEMU_PROFILE = "relJoin"
$joinProc = Start-Process -FilePath $exe -ArgumentList "join" -WorkingDirectory $outDir `
	-RedirectStandardOutput $joinOut -PassThru -NoNewWindow
Remove-Item Env:\EOSEMU_PROFILE
Remove-Item Env:\EOSEMU_P2P_TESTLOSS

$joinProc.WaitForExit(45000) | Out-Null
$hostProc.WaitForExit(30000) | Out-Null
if (-not $joinProc.HasExited) { $joinProc.Kill() }
if (-not $hostProc.HasExited) { $hostProc.Kill() }

Write-Host "===== HOST ====="; Get-Content $hostOut | Out-Host
Write-Host "===== JOIN ====="; Get-Content $joinOut | Out-Host

$hostPass = (Select-String -Path $hostOut -Pattern "REL-HOST PASS" -Quiet)
$joinPass = (Select-String -Path $joinOut -Pattern "REL-JOIN PASS" -Quiet)
if ($hostPass -and $joinPass) { Write-Host "P2P RELIABLE: PASS"; exit 0 }
Write-Host "P2P RELIABLE: FAIL (host=$hostPass join=$joinPass)"; exit 1
