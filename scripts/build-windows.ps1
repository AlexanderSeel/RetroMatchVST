param(
    [string]$JuceDir = "",
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [switch]$CopyPlugin,
    [switch]$RunTests,
    [switch]$UseDocker,
    [switch]$InstallMissing,
    [switch]$NonInteractive,
    [switch]$SkipPrerequisiteCheck
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

function Refresh-ProcessPath {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = @($machine, $user) -join ";"
}

function Test-Command([string]$Name) {
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Ask-YesNo([string]$Question) {
    if ($InstallMissing) { return $true }
    if ($NonInteractive) { return $false }

    while ($true) {
        $answer = (Read-Host "$Question [Y/n]").Trim().ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($answer) -or $answer -in @("y", "yes")) { return $true }
        if ($answer -in @("n", "no")) { return $false }
    }
}

function Get-CMakeVersion {
    if (-not (Test-Command cmake)) { return $null }
    $line = (& cmake --version | Select-Object -First 1)
    if ($line -match "(\d+)\.(\d+)\.(\d+)") {
        return [Version]::new([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
    }
    return $null
}

function Get-VsWherePath {
    $candidate = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Get-VisualStudioWithCpp {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) { return @() }

    $json = (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($json)) { return @() }
    return @($json | ConvertFrom-Json | Sort-Object { [Version]$_.installationVersion } -Descending)
}

function Get-SupportedVisualStudio {
    if (-not (Test-Command cmake)) { return $null }

    $cmakeHelp = (& cmake --help | Out-String)
    $installations = @(Get-VisualStudioWithCpp)

    foreach ($vs in $installations) {
        $major = ([Version]$vs.installationVersion).Major
        if ($major -ge 18 -and $cmakeHelp -match "Visual Studio 18 2026") {
            return [pscustomobject]@{ Generator = "Visual Studio 18 2026"; Installation = $vs.installationPath }
        }
        if ($major -ge 17 -and $cmakeHelp -match "Visual Studio 17 2022") {
            return [pscustomobject]@{ Generator = "Visual Studio 17 2022"; Installation = $vs.installationPath }
        }
    }

    return $null
}

function Install-WingetPackage([string]$Id, [string]$Override = "") {
    $wingetArgs = @(
        "install", "--id", $Id, "--exact", "--source", "winget",
        "--accept-package-agreements", "--accept-source-agreements"
    )
    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        $wingetArgs += @("--override", $Override)
    }

    Write-Host "Installing $Id ..." -ForegroundColor Cyan
    & winget @wingetArgs
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed while installing $Id (exit code $LASTEXITCODE)."
    }
}

function Ensure-Prerequisites {
    $missing = [System.Collections.Generic.List[string]]::new()
    $cmakeVersion = Get-CMakeVersion

    $vsInstallations = @(Get-VisualStudioWithCpp)
    if (-not (Test-Command git)) { $missing.Add("Git for Windows") }
    if ($null -eq $cmakeVersion -or $cmakeVersion -lt [Version]"3.24.0") { $missing.Add("CMake 3.24+") }
    elseif ($vsInstallations.Count -gt 0 -and $null -eq (Get-SupportedVisualStudio)) { $missing.Add("Current CMake with support for the installed Visual Studio generator") }
    if ($vsInstallations.Count -eq 0) { $missing.Add("Visual Studio C++ Build Tools (2022/2026)") }

    if ($missing.Count -eq 0) {
        $vs = Get-SupportedVisualStudio
        Write-Host "Prerequisites OK: Git, CMake $cmakeVersion, $($vs.Generator)." -ForegroundColor Green
        return
    }

    Write-Host ""
    Write-Host "Missing or unsupported build prerequisites:" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  - $_" }

    if (-not (Ask-YesNo "Install the missing prerequisites automatically with winget?")) {
        throw "Build prerequisites are missing. Re-run with -InstallMissing to install automatically, or see docs/BUILD.md."
    }

    if (-not (Test-Command winget)) {
        throw "winget is required for automatic installation. Install/update Microsoft App Installer, then run the build again."
    }

    if ($missing -contains "Git for Windows") {
        Install-WingetPackage "Git.Git"
    }
    if (($missing -contains "CMake 3.24+") -or ($missing -contains "Current CMake with support for the installed Visual Studio generator")) {
        Install-WingetPackage "Kitware.CMake"
    }
    if ($missing -contains "Visual Studio C++ Build Tools (2022/2026)") {
        Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    }

    Refresh-ProcessPath

    $cmakeVersion = Get-CMakeVersion
    $vs = Get-SupportedVisualStudio
    $stillMissing = @()
    if (-not (Test-Command git)) { $stillMissing += "Git" }
    if ($null -eq $cmakeVersion -or $cmakeVersion -lt [Version]"3.24.0") { $stillMissing += "CMake 3.24+" }
    if ($null -eq $vs) { $stillMissing += "Visual Studio C++ toolchain" }

    if ($stillMissing.Count -gt 0) {
        throw "Installation finished, but these tools are not visible yet: $($stillMissing -join ', '). A reboot or a new PowerShell session may be required."
    }

    Write-Host "Prerequisite installation completed successfully." -ForegroundColor Green
}

if ($UseDocker) {
    $dockerScript = Join-Path $PSScriptRoot "build-docker.ps1"
    if (-not (Test-Path $dockerScript)) { throw "Docker build helper not found: $dockerScript" }

    $dockerArgs = @("-Target", "Windows", "-Config", $Config)
    if ($InstallMissing) { $dockerArgs += "-InstallDocker" }
    if ($NonInteractive) { $dockerArgs += "-NonInteractive" }
    & $dockerScript @dockerArgs
    exit $LASTEXITCODE
}

if (-not $SkipPrerequisiteCheck) {
    Ensure-Prerequisites
}

if (-not (Test-Command cmake)) { throw "CMake is not available in PATH." }
if (-not (Test-Command git)) { throw "Git is not available in PATH." }

$vs = Get-SupportedVisualStudio
if ($null -eq $vs) {
    throw "No supported Visual Studio C++ installation was detected. Install VS 2022/2026 Build Tools with the C++ workload."
}
$Generator = $vs.Generator

if ([string]::IsNullOrWhiteSpace($JuceDir)) {
    $JuceDir = Join-Path $Root "extern\JUCE"
    if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
        Write-Host "JUCE 9.0.1 not found locally. Cloning official JUCE repository..."
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $JuceDir) | Out-Null
        git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE.git $JuceDir
        if ($LASTEXITCODE -ne 0) { throw "JUCE clone failed. Check network/proxy access to github.com." }
    }
}

if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
    throw "JUCE directory is invalid: $JuceDir"
}

$BuildDir = Join-Path $Root "build-windows"
$Copy = if ($CopyPlugin) { "ON" } else { "OFF" }
$Tests = if ($RunTests) { "ON" } else { "OFF" }

Write-Host "Configuring with $Generator ..." -ForegroundColor Cyan
cmake -S $Root -B $BuildDir -G $Generator -A x64 `
    "-DRETROMATCH_JUCE_DIR=$JuceDir" `
    "-DRETROMATCH_COPY_PLUGIN=$Copy" `
    "-DRETROMATCH_BUILD_TESTS=$Tests"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

cmake --build $BuildDir --config $Config --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone --parallel
if ($LASTEXITCODE -ne 0) { throw "Plug-in build failed." }

if ($RunTests) {
    cmake --build $BuildDir --config $Config --target RetroMatchTests --parallel
    if ($LASTEXITCODE -ne 0) { throw "Test build failed." }
    ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
}

$Artefacts = Join-Path $BuildDir "RetroMatchSynth_artefacts\$Config"
Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
Write-Host "VST3:       $Artefacts\VST3\RetroMatch Synth.vst3"
Write-Host "Standalone: $Artefacts\Standalone\RetroMatch Synth.exe"
