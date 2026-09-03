param(
    [ValidateSet("Windows", "Linux")][string]$Target = "Windows",
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [string]$OutputDir = "",
    [switch]$InstallDocker,
    [switch]$NonInteractive,
    [switch]$NoCache,
    [switch]$KeepImage
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
    if ($InstallDocker) { return $true }
    if ($NonInteractive) { return $false }

    while ($true) {
        $answer = (Read-Host "$Question [Y/n]").Trim().ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($answer) -or $answer -in @("y", "yes")) { return $true }
        if ($answer -in @("n", "no")) { return $false }
    }
}

function Wait-DockerDaemon {
    for ($i = 0; $i -lt 45; $i++) {
        & docker info *> $null
        if ($LASTEXITCODE -eq 0) { return $true }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Ensure-Docker {
    if (-not (Test-Command docker)) {
        Write-Host "Docker Desktop is not installed or docker.exe is not in PATH." -ForegroundColor Yellow
        if (-not (Ask-YesNo "Install Docker Desktop with winget?")) {
            throw "Docker is required for -UseDocker. Install Docker Desktop or use the native build path."
        }
        if (-not (Test-Command winget)) {
            throw "winget is required for automatic Docker Desktop installation. Install/update Microsoft App Installer first."
        }

        & winget install --id Docker.DockerDesktop --exact --source winget --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { throw "Docker Desktop installation failed (exit code $LASTEXITCODE)." }
        Refresh-ProcessPath
    }

    if (-not (Test-Command docker)) {
        throw "Docker Desktop was installed but docker.exe is not visible in this shell. Start Docker Desktop and open a new PowerShell window."
    }

    & docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        if ((& docker desktop status 2>$null | Out-String) -notmatch "running") {
            Write-Host "Starting Docker Desktop ..." -ForegroundColor Cyan
            & docker desktop start | Out-Host
        }
        if (-not (Wait-DockerDaemon)) {
            throw "Docker Desktop did not become ready. Open Docker Desktop once, complete any WSL/virtualization setup, then rerun the command."
        }
    }
}

function Ensure-DockerEngine([string]$Desired) {
    $current = (& docker info --format '{{.OSType}}' 2>$null | Out-String).Trim().ToLowerInvariant()
    if ($current -eq $Desired) { return }

    if ($Target -eq "Windows") {
        $productName = (Get-CimInstance Win32_OperatingSystem).Caption
        if ($productName -match "Home|Education") {
            throw "Native Windows containers require Windows Pro/Enterprise. Use -Target Linux for clean-room verification, or GitHub Actions for Windows VST3 binaries."
        }
    }

    Write-Host "Switching Docker Desktop engine from '$current' to '$Desired' ..." -ForegroundColor Cyan
    & docker desktop engine use $Desired | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Automatic Docker engine switch failed. In Docker Desktop switch to $Desired containers and rerun the build."
    }
    if (-not (Wait-DockerDaemon)) {
        throw "Docker switched engines but did not become ready. Restart Docker Desktop and rerun the build."
    }
}

Ensure-Docker
$desiredEngine = if ($Target -eq "Windows") { "windows" } else { "linux" }
Ensure-DockerEngine $desiredEngine

$dockerfile = if ($Target -eq "Windows") { "Dockerfile.windows" } else { "Dockerfile" }
$dockerfilePath = Join-Path $Root $dockerfile
if (-not (Test-Path $dockerfilePath)) { throw "Dockerfile not found: $dockerfilePath" }

$image = "retromatch-build-$($Target.ToLowerInvariant()):1.0.0"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $Root "dist\docker-$($Target.ToLowerInvariant())-$($Config.ToLowerInvariant())"
}

$buildArgs = @("build", "--file", $dockerfilePath, "--tag", $image, "--build-arg", "CONFIG=$Config")
if ($NoCache) { $buildArgs += "--no-cache" }
$buildArgs += $Root

Write-Host "Building isolated $Target image '$image' ..." -ForegroundColor Cyan
& docker @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Docker image build failed." }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$container = "retromatch-export-$([guid]::NewGuid().ToString('N').Substring(0, 10))"
try {
    & docker create --name $container $image | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not create export container." }

    $containerPath = if ($Target -eq "Windows") { "${container}:C:/out/." } else { "${container}:/out/." }
    & docker cp $containerPath $OutputDir
    if ($LASTEXITCODE -ne 0) { throw "Could not copy build artifacts from the container." }
}
finally {
    & docker rm -f $container *> $null
}

if (-not $KeepImage) {
    & docker image rm $image *> $null
}

Write-Host ""
Write-Host "Container build complete." -ForegroundColor Green
Write-Host "Target:    $Target"
Write-Host "Artifacts: $OutputDir"
if ($Target -eq "Linux") {
    Write-Host "Note: Linux-container artifacts are Linux binaries; use the Windows container or GitHub Actions for Windows VST3/EXE."
}
