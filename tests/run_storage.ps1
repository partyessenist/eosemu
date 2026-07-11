# Builds tests/storage.c and runs it twice: a write pass and a fresh-process
# read pass. With [Storage] Persist = true and a CacheDirectory, the second
# process must read back what the first wrote (the on-disk mirror), proving
# PlayerDataStorage persistence. Also covers transfer-request lifetime safety
# and filename validation.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $root
$sdkInc = Join-Path $root "third_party\EOSSDK\SDK\Include"
$sdkLib = Join-Path $root "third_party\EOSSDK\SDK\Lib\EOSSDK-Win64-Shipping.lib"
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
$outDir = Join-Path $env:TEMP "eosemu_storage"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (-not (Test-Path $dll)) { throw "Build the DLL first" }
Copy-Item $dll $outDir -Force

$cacheDir = Join-Path $outDir "cache"
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

$ini = @"
[Storage]
Persist = true
"@
$iniPath = Join-Path $outDir "eosemu.ini"
Set-Content -Path $iniPath -Value $ini -Encoding utf8

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$installerDir = Split-Path $vswhere -Parent

$src = Join-Path $root "tests\storage.c"
$exe = Join-Path $outDir "storage.exe"
$obj = Join-Path $outDir "storage.obj"

$prevEA = $ErrorActionPreference; $ErrorActionPreference = "Continue"
$cmd = "set `"PATH=%PATH%;$installerDir`" && call `"$vcvars`" && cl /nologo /W3 /I `"$sdkInc`" `"$src`" /Fe:`"$exe`" /Fo:`"$obj`" /link `"$sdkLib`""
cmd /c $cmd 2>&1 | Out-Host
$ErrorActionPreference = $prevEA
if (-not (Test-Path $exe)) { throw "compile failed" }

# Same profile for both passes so the PUID (and mirror path) matches.
$env:EOSEMU_CONFIG = $iniPath
$env:EOSEMU_PROFILE = "storageA"

Write-Host "--- write pass ---"
& $exe write $cacheDir
$writeCode = $LASTEXITCODE

Write-Host "--- mirror on disk ---"
$mirror = Join-Path $cacheDir "eosemu_pds"
if (Test-Path $mirror) {
	Get-ChildItem -Recurse -File $mirror | ForEach-Object { Write-Host "   $($_.FullName.Substring($mirror.Length)) ($($_.Length) bytes)" }
} else {
	Write-Host "   MISSING"
}

Write-Host "--- read pass (fresh process) ---"
& $exe read $cacheDir
$readCode = $LASTEXITCODE

Remove-Item Env:\EOSEMU_CONFIG -ErrorAction SilentlyContinue
Remove-Item Env:\EOSEMU_PROFILE -ErrorAction SilentlyContinue

if ($writeCode -eq 0 -and $readCode -eq 0) { Write-Host "STORAGE: PASS"; exit 0 }
Write-Host "STORAGE: FAIL (write=$writeCode read=$readCode)"; exit 1
