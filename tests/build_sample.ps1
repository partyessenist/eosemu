# Builds an Epic sample and drops EOSEmu's DLL into its output directory.
#
# The samples target the VS2017 (v141) toolset and the 10.0.17763 Windows SDK.
# Rather than editing the read-only sample projects, this overrides the toolset
# and SDK on the MSBuild command line to whatever is installed. Never writes into
# SDK/ and never edits a .vcxproj.
#
#   .\build_sample.ps1 -Project ..\..\Samples\AntiCheat\Server\AntiCheatServer.vcxproj `
#                      -Config Debug -OutSubdir Bin\Win64\Debug
#
# Then run the sample's exe from its OutDir; it resolves EOSSDK-Win64-Shipping.dll
# (now EOSEmu's) by name.
param(
	[Parameter(Mandatory = $true)][string]$Project,
	[string]$Config = "Debug",
	[string]$Platform = "x64",
	[Parameter(Mandatory = $true)][string]$OutSubdir
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
$vs = & $vswhere -latest -property installationPath
$msbuild = Join-Path $vs "MSBuild\Current\Bin\MSBuild.exe"

# Pick the newest installed toolset + Windows SDK.
$toolset = (Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$toolsetArg = "v143"  # 14.3x
$sdk = (Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" -Directory |
	Where-Object { $_.Name -match '^10\.' } | Sort-Object Name -Descending | Select-Object -First 1).Name

Write-Host "Building $Project [$Config|$Platform] toolset=$toolsetArg sdk=$sdk (MSVC $toolset)"
& $msbuild $Project /p:Configuration=$Config /p:Platform=$Platform `
	/p:PlatformToolset=$toolsetArg /p:WindowsTargetPlatformVersion=$sdk /nologo /v:minimal /m
if ($LASTEXITCODE -ne 0) { throw "sample build failed" }

$projDir = Split-Path -Parent (Resolve-Path $Project)
$outDir = Join-Path $projDir $OutSubdir
& (Join-Path $PSScriptRoot "deploy_to_sample.ps1") -SampleOutDir $outDir
Write-Host "Sample built and EOSEmu DLL deployed. Run: $outDir"
