# Builds tests/p2p_edge.c and runs two instances (host + join), validating
# queue-full backpressure (no reliable loss), EOS_CCR_ConnectionIgnored, and
# PeerConnectionInterrupted -> ConnectionClosed(TimedOut) on a vanished peer.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_p2p_edge"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\p2p_edge.c"
$exe = Join-Path $outDir "p2p_edge.exe"
$obj = Join-Path $outDir "p2p_edge.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

Write-Host "--- launching host + join ---"
$hostOut = Join-Path $outDir "host.txt"
$joinOut = Join-Path $outDir "join.txt"

$env:EOSEMU_PROFILE = "edgeHostA"
$hostProc = Start-Process -FilePath $exe -ArgumentList "host" -WorkingDirectory $outDir `
	-RedirectStandardOutput $hostOut -PassThru -NoNewWindow
Start-Sleep -Milliseconds 800
$env:EOSEMU_PROFILE = "edgeJoinB"
$joinProc = Start-Process -FilePath $exe -ArgumentList "join" -WorkingDirectory $outDir `
	-RedirectStandardOutput $joinOut -PassThru -NoNewWindow
Remove-Item Env:\EOSEMU_PROFILE

# The joiner deliberately outlives the host (interruption phase, ~35s).
$hostProc.WaitForExit(90000) | Out-Null
$joinProc.WaitForExit(90000) | Out-Null
if (-not $joinProc.HasExited) { $joinProc.Kill() }
if (-not $hostProc.HasExited) { $hostProc.Kill() }

Write-Host "===== HOST ====="; Get-Content $hostOut | Out-Host
Write-Host "===== JOIN ====="; Get-Content $joinOut | Out-Host

$joinPass = (Select-String -Path $joinOut -Pattern "EDGE-JOIN PASS" -Quiet)
$hostPass = (Select-String -Path $hostOut -Pattern "EDGE-HOST PASS" -Quiet)
if ($joinPass -and $hostPass) { Write-Host "P2P EDGE: PASS"; exit 0 }
Write-Host "P2P EDGE: FAIL (host=$hostPass join=$joinPass)"; exit 1
