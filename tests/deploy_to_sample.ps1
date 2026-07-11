# Copies the freshly built EOSEmu DLL over the real SDK DLL in a sample's output
# directory. Run AFTER the sample builds (its post-build xcopy drops the real
# DLL there; this overwrites it). Never writes into SDK/ or edits the sample.
#
#   .\deploy_to_sample.ps1 -SampleOutDir ..\..\Samples\AntiCheat\Server\Bin\Win64\Debug
param(
	[Parameter(Mandatory = $true)][string]$SampleOutDir
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root "build\Debug\EOSSDK-Win64-Shipping.dll"
if (-not (Test-Path $dll)) { throw "EOSEmu DLL not built: $dll" }
if (-not (Test-Path $SampleOutDir)) { throw "Sample output dir not found (build the sample first): $SampleOutDir" }
Copy-Item $dll (Join-Path $SampleOutDir "EOSSDK-Win64-Shipping.dll") -Force
Write-Host "Deployed EOSEmu DLL -> $SampleOutDir"
