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

function Invoke-DockerMonitored {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Activity,
        [int]$HeartbeatSeconds = 20
    )

    $dockerExe = (Get-Command docker -ErrorAction Stop).Source
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $dockerExe
    $psi.UseShellExecute = $false
    foreach ($arg in $Arguments) {
        [void]$psi.ArgumentList.Add([string]$arg)
    }

    # Keep this variable explicitly local. The script can be invoked from a
    # caller that already uses $process, and use WaitForExit rather than the
    # HasExited/Refresh sequence to avoid a race while docker.exe is exiting.
    $dockerProcess = $null
    try {
        $dockerProcess = [System.Diagnostics.Process]::Start($psi)
        if ($null -eq $dockerProcess) { throw "Could not start Docker for: $Activity" }

        $nextHeartbeat = (Get-Date).AddSeconds($HeartbeatSeconds)
        while (-not $dockerProcess.WaitForExit(2000)) {
            if ((Get-Date) -ge $nextHeartbeat) {
                Write-Host ("[{0}] {1} is still running (docker PID {2}). Windows image layer extraction/registration can be silent after download completes." -f (Get-Date -Format "HH:mm:ss"), $Activity, $dockerProcess.Id) -ForegroundColor DarkGray
                $nextHeartbeat = (Get-Date).AddSeconds($HeartbeatSeconds)
            }
        }

        $exitCode = $dockerProcess.ExitCode
    }
    finally {
        if ($null -ne $dockerProcess) { $dockerProcess.Dispose() }
    }

    if ($exitCode -ne 0) {
        throw "$Activity failed (docker exit code $exitCode)."
    }
}

function Show-DockerDiagnostics {
    Write-Host ""
    Write-Host "Docker diagnostics:" -ForegroundColor Cyan
    try {
        $engine = (& docker info --format 'OS={{.OSType}}; Storage={{.Driver}}; Root={{.DockerRootDir}}' 2>$null | Out-String).Trim()
        if (-not [string]::IsNullOrWhiteSpace($engine)) { Write-Host "  $engine" }
    } catch {}

    try {
        $systemDriveName = $env:SystemDrive.TrimEnd(':')
        $drive = Get-PSDrive -Name $systemDriveName -ErrorAction Stop
        $freeGb = [math]::Round($drive.Free / 1GB, 1)
        Write-Host "  Host $($env:SystemDrive) free space: $freeGb GB"
        if ($freeGb -lt 35) {
            Write-Warning "Windows container images and Visual Studio Build Tools consume substantial disk space. Less than 35 GB is free on the system drive. Docker Desktop may use another data location, but check Docker storage before continuing."
        }
    } catch {}

    try {
        & docker system df | Out-Host
    } catch {}
    Write-Host ""
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

function Export-WindowsContainerArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Configuration
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) "retromatch-export-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

    try {
        $artifactRoot = "C:/build/RetroMatchSynth_artefacts/$Configuration"
        & docker cp "${Container}:${artifactRoot}/VST3" $stagingRoot
        if ($LASTEXITCODE -ne 0) { throw "Could not copy VST3 artifacts from the build container." }
        & docker cp "${Container}:${artifactRoot}/Standalone" $stagingRoot
        if ($LASTEXITCODE -ne 0) { throw "Could not copy Standalone artifacts from the build container." }

        foreach ($name in @("VST3", "Standalone")) {
            $source = Join-Path $stagingRoot $name
            if (-not (Test-Path -LiteralPath $source)) {
                throw "Expected staged artifact directory was not created: $source"
            }
            $destinationPath = Join-Path $Destination $name
            if (Test-Path -LiteralPath $destinationPath) {
                Remove-Item -LiteralPath $destinationPath -Recurse -Force
            }
            Move-Item -LiteralPath $source -Destination $Destination -Force
        }
    }
    finally {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Ensure-Docker
$desiredEngine = if ($Target -eq "Windows") { "windows" } else { "linux" }
Ensure-DockerEngine $desiredEngine
Show-DockerDiagnostics

$dockerfile = if ($Target -eq "Windows") { "Dockerfile.windows" } else { "Dockerfile" }
$dockerfilePath = Join-Path $Root $dockerfile
if (-not (Test-Path $dockerfilePath)) { throw "Dockerfile not found: $dockerfilePath" }

$baseImage = if ($Target -eq "Windows") {
    "mcr.microsoft.com/dotnet/framework/runtime:4.8-windowsservercore-ltsc2022"
} else {
    "ubuntu:24.04"
}

Write-Host "Pre-pulling base image: $baseImage" -ForegroundColor Cyan
Write-Host "Windows base images are large. After all layers report 'Download complete', Docker may remain silent while it expands and registers those layers." -ForegroundColor DarkGray
Invoke-DockerMonitored -Arguments @("pull", $baseImage) -Activity "Base image pull/extraction"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $Root "dist\docker-$($Target.ToLowerInvariant())-$($Config.ToLowerInvariant())"
}

if ($Target -eq "Windows") {
    # Do not docker-build the compiler output into the final image. On Docker
    # Desktop for Windows, committing/importing that very large writable layer
    # can fail with hcsshim::ImportLayer (0x3) even after compilation and CTest
    # have completed successfully. Build only the small source/toolchain image,
    # compile in a disposable container, and copy artifacts directly from it.
    $sourceImage = "retromatch-source-windows:1.0.0"
    $sourceBuildArgs = @(
        "build", "--file", $dockerfilePath,
        "--target", "source",
        "--tag", $sourceImage,
        "--build-arg", "CONFIG=$Config"
    )
    if ($NoCache) { $sourceBuildArgs += "--no-cache" }
    $sourceBuildArgs += $Root

    Write-Host "Building reusable Windows toolchain/source image '$sourceImage' ..." -ForegroundColor Cyan
    Invoke-DockerMonitored -Arguments $sourceBuildArgs -Activity "Windows Docker source/toolchain image build"

    $container = "retromatch-buildrun-$([guid]::NewGuid().ToString('N').Substring(0, 10))"
    try {
        Write-Host "Creating disposable Windows build container '$container' ..." -ForegroundColor Cyan
        & docker create --name $container --memory 4g $sourceImage C:\Windows\System32\cmd.exe /D /C C:\src\scripts\build-container-windows.cmd $Config | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Could not create disposable Windows build container." }

        Write-Host "Compiling and testing RetroMatch inside the disposable container..." -ForegroundColor Cyan
        Invoke-DockerMonitored -Arguments @("start", "--attach", $container) -Activity "Windows container compile/test"

        Write-Host "Exporting build artifacts directly from the stopped container..." -ForegroundColor Cyan
        Export-WindowsContainerArtifacts -Container $container -Destination $OutputDir -Configuration $Config
    }
    finally {
        & docker rm -f $container *> $null
    }

    if (-not $KeepImage) {
        & docker image rm $sourceImage *> $null
    }

    Write-Host ""
    Write-Host "Container build complete." -ForegroundColor Green
    Write-Host "Target:    Windows"
    Write-Host "Artifacts: $OutputDir"
    Write-Host "Mode:      disposable compile container (no final compiler-output image layer)" -ForegroundColor DarkGray
    exit 0
}

# Linux retains the conventional image-build/export flow.
$image = "retromatch-build-linux:1.0.0"
$buildArgs = @("build", "--file", $dockerfilePath, "--tag", $image, "--build-arg", "CONFIG=$Config")
if ($NoCache) { $buildArgs += "--no-cache" }
$buildArgs += $Root

Write-Host "Building isolated Linux image '$image' ..." -ForegroundColor Cyan
Invoke-DockerMonitored -Arguments $buildArgs -Activity "Linux Docker image build"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$container = "retromatch-export-$([guid]::NewGuid().ToString('N').Substring(0, 10))"
$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) "retromatch-export-$([guid]::NewGuid().ToString('N'))"
$stagedOut = Join-Path $stagingRoot "out"
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
try {
    & docker create --name $container $image | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not create export container." }

    $containerPath = "${container}:/out"
    & docker cp $containerPath $stagingRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not copy build artifacts from the container." }
    if (-not (Test-Path $stagedOut)) { throw "Docker artifact export completed but the staged 'out' directory was not created." }

    Get-ChildItem -LiteralPath $stagedOut -Force | ForEach-Object {
        $destination = Join-Path $OutputDir $_.Name
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
        Move-Item -LiteralPath $_.FullName -Destination $OutputDir -Force
    }
}
finally {
    & docker rm -f $container *> $null
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not $KeepImage) {
    & docker image rm $image *> $null
}

Write-Host ""
Write-Host "Container build complete." -ForegroundColor Green
Write-Host "Target:    Linux"
Write-Host "Artifacts: $OutputDir"
Write-Host "Note: Linux-container artifacts are Linux binaries; use the Windows container or GitHub Actions for Windows VST3/EXE."
