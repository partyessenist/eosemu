# Builds tests/invite.c and runs the overlay invite-accept / join-friend flow as
# two paired processes, twice:
#   Scenario A (invite): host + invitee. The invitee AUTOACCEPTs the host's lobby
#                        invite, resolves it by invite id, and joins the room.
#   Scenario B (join):   host + joiner. The joiner AUTOJOINs the discovered room
#                        via a JoinLobbyAccepted UiEventId it resolves + acks.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_invite"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\invite.c"
$exe = Join-Path $outDir "invite.exe"
$obj = Join-Path $outDir "invite.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

# Runs one host + one guest process pair, returns $true if both print PASS.
function Run-Pair($guestRole, $hostProfile, $guestProfile, $guestEnvName) {
	$hostOut = Join-Path $outDir "host_$guestRole.txt"
	$guestOut = Join-Path $outDir "$guestRole.txt"

	$env:EOSEMU_PROFILE = $hostProfile
	Remove-Item Env:\EOSEMU_OVERLAY_AUTOACCEPT -ErrorAction SilentlyContinue
	Remove-Item Env:\EOSEMU_OVERLAY_AUTOJOIN -ErrorAction SilentlyContinue
	$hostProc = Start-Process -FilePath $exe -ArgumentList "host" -WorkingDirectory $outDir `
		-RedirectStandardOutput $hostOut -PassThru -NoNewWindow
	Start-Sleep -Milliseconds 600

	$env:EOSEMU_PROFILE = $guestProfile
	Set-Item "Env:\$guestEnvName" "1"
	$guestProc = Start-Process -FilePath $exe -ArgumentList $guestRole -WorkingDirectory $outDir `
		-RedirectStandardOutput $guestOut -PassThru -NoNewWindow
	Remove-Item "Env:\$guestEnvName" -ErrorAction SilentlyContinue
	Remove-Item Env:\EOSEMU_PROFILE -ErrorAction SilentlyContinue

	$guestProc.WaitForExit(60000) | Out-Null
	$hostProc.WaitForExit(60000) | Out-Null
	if (-not $guestProc.HasExited) { $guestProc.Kill() }
	if (-not $hostProc.HasExited) { $hostProc.Kill() }

	Write-Host "===== HOST ($guestRole) ====="; Get-Content $hostOut | Out-Host
	Write-Host "===== $($guestRole.ToUpper()) ====="; Get-Content $guestOut | Out-Host

	$hostPass = (Select-String -Path $hostOut -Pattern "INVITE-HOST PASS" -Quiet)
	$guestPass = (Select-String -Path $guestOut -Pattern "PASS" -Quiet)
	return ($hostPass -and $guestPass)
}

Write-Host "--- Scenario A: invite-accept ---"
$aPass = Run-Pair "invitee" "invHostA" "invGuestA" "EOSEMU_OVERLAY_AUTOACCEPT"

Write-Host "--- Scenario B: join-friend (UiEventId) ---"
$bPass = Run-Pair "joiner" "invHostB" "invJoinB" "EOSEMU_OVERLAY_AUTOJOIN"

if ($aPass -and $bPass) { Write-Host "INVITE FLOW: PASS"; exit 0 }
Write-Host "INVITE FLOW: FAIL (invite=$aPass join=$bPass)"; exit 1
