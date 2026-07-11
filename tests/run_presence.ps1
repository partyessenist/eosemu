# Builds tests/presence.c and runs two instances (setter + checker) on this
# machine with distinct EOSEMU_PROFILE values, validating that rich presence
# (status/richtext/joininfo/data records/product id) set on one instance is
# replicated over the LAN and served by CopyPresence/GetJoinInfo on the other.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_presence"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\presence.c"
$exe = Join-Path $outDir "presence.exe"
$obj = Join-Path $outDir "presence.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

Write-Host "--- launching setter + checker ---"
$setOut = Join-Path $outDir "setter.txt"
$chkOut = Join-Path $outDir "checker.txt"

# Windows PowerShell 5.1 Start-Process has no -Environment; a child inherits the
# parent env at launch, so set EOSEMU_PROFILE just before each launch.
$env:EOSEMU_PROFILE = "presA"
$setProc = Start-Process -FilePath $exe -ArgumentList "setter" -WorkingDirectory $outDir `
	-RedirectStandardOutput $setOut -PassThru -NoNewWindow
Start-Sleep -Milliseconds 800
$env:EOSEMU_PROFILE = "presB"
$chkProc = Start-Process -FilePath $exe -ArgumentList "checker" -WorkingDirectory $outDir `
	-RedirectStandardOutput $chkOut -PassThru -NoNewWindow
Remove-Item Env:\EOSEMU_PROFILE

$chkProc.WaitForExit(45000) | Out-Null
$setProc.WaitForExit(30000) | Out-Null
if (-not $chkProc.HasExited) { $chkProc.Kill() }
if (-not $setProc.HasExited) { $setProc.Kill() }

Write-Host "===== SETTER ====="; Get-Content $setOut | Out-Host
Write-Host "===== CHECKER ====="; Get-Content $chkOut | Out-Host

$setPass = (Select-String -Path $setOut -Pattern "PRESENCE-SET PASS" -Quiet)
$chkPass = (Select-String -Path $chkOut -Pattern "PRESENCE-CHECK PASS" -Quiet)
if ($setPass -and $chkPass) { Write-Host "PRESENCE ACCEPTANCE: PASS"; exit 0 }
Write-Host "PRESENCE ACCEPTANCE: FAIL (setter=$setPass checker=$chkPass)"; exit 1
