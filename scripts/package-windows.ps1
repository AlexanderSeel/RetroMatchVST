param([ValidateSet("Debug", "Release")][string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Artefacts = Join-Path $Root "build-windows\RetroMatchSynth_artefacts\$Config"
$Out = Join-Path $Root "dist"
New-Item -ItemType Directory -Force $Out | Out-Null
$Stage = Join-Path $Out "RetroMatchSynth-Windows-x64"
Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Stage | Out-Null

$Vst = Join-Path $Artefacts "VST3\RetroMatch Synth.vst3"
$Exe = Join-Path $Artefacts "Standalone\RetroMatch Synth.exe"
if (-not (Test-Path $Vst)) { throw "VST3 build not found. Run scripts/build-windows.ps1 first." }
Copy-Item -Recurse $Vst $Stage
if (Test-Path $Exe) { Copy-Item $Exe $Stage }
Copy-Item (Join-Path $Root "README.md") $Stage
Copy-Item (Join-Path $Root "docs\BUILD.md") $Stage

$Zip = Join-Path $Out "RetroMatchSynth-Windows-x64-$Config.zip"
Remove-Item $Zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip
Write-Host "Created $Zip"
