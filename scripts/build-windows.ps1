param(
    [string]$JuceDir = "",
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [switch]$CopyPlugin,
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required tool '$Name' was not found in PATH. See docs/BUILD.md."
    }
}

Require-Command cmake
Require-Command git

if ([string]::IsNullOrWhiteSpace($JuceDir)) {
    $JuceDir = Join-Path $Root "extern\JUCE"
    if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
        Write-Host "JUCE 9.0.1 not found locally. Cloning official JUCE repository..."
        git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE.git $JuceDir
    }
}

if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
    throw "JUCE directory is invalid: $JuceDir"
}

$CmakeHelp = (cmake --help | Out-String)
if ($CmakeHelp -match "Visual Studio 18 2026") {
    $Generator = "Visual Studio 18 2026"
} elseif ($CmakeHelp -match "Visual Studio 17 2022") {
    $Generator = "Visual Studio 17 2022"
} else {
    throw "No supported Visual Studio CMake generator found. Install Visual Studio 2026 or 2022 with Desktop development with C++."
}

$BuildDir = Join-Path $Root "build-windows"
$Copy = if ($CopyPlugin) { "ON" } else { "OFF" }
$Tests = if ($RunTests) { "ON" } else { "OFF" }

cmake -S $Root -B $BuildDir -G $Generator -A x64 `
    "-DRETROMATCH_JUCE_DIR=$JuceDir" `
    "-DRETROMATCH_COPY_PLUGIN=$Copy" `
    "-DRETROMATCH_BUILD_TESTS=$Tests"

cmake --build $BuildDir --config $Config --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone --parallel

if ($RunTests) {
    cmake --build $BuildDir --config $Config --target RetroMatchTests --parallel
    ctest --test-dir $BuildDir -C $Config --output-on-failure
}

$Artefacts = Join-Path $BuildDir "RetroMatchSynth_artefacts\$Config"
Write-Host ""
Write-Host "Build complete."
Write-Host "VST3:       $Artefacts\VST3\RetroMatch Synth.vst3"
Write-Host "Standalone: $Artefacts\Standalone\RetroMatch Synth.exe"
