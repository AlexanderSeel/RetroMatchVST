param(
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BaseImage = "mcr.microsoft.com/dotnet/framework/runtime:4.8-windowsservercore-ltsc2022"

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
    foreach ($arg in $Arguments) { [void]$psi.ArgumentList.Add([string]$arg) }

    $dockerProcess = $null
    try {
        $dockerProcess = [System.Diagnostics.Process]::Start($psi)
        if ($null -eq $dockerProcess) { throw "Could not start Docker for: $Activity" }

        $nextHeartbeat = (Get-Date).AddSeconds($HeartbeatSeconds)
        while (-not $dockerProcess.WaitForExit(2000)) {
            if ((Get-Date) -ge $nextHeartbeat) {
                Write-Host ("[{0}] {1} is still running (docker PID {2})." -f (Get-Date -Format "HH:mm:ss"), $Activity, $dockerProcess.Id) -ForegroundColor DarkGray
                $nextHeartbeat = (Get-Date).AddSeconds($HeartbeatSeconds)
            }
        }
        $exitCode = $dockerProcess.ExitCode
    }
    finally {
        if ($null -ne $dockerProcess) { $dockerProcess.Dispose() }
    }

    if ($exitCode -ne 0) { throw "$Activity failed (docker exit code $exitCode)." }
}

function Export-Artifacts {
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Configuration
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) "retromatch-direct-export-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null

    try {
        $artifactRoot = "C:/build/RetroMatchSynth_artefacts/$Configuration"
        & docker cp "${Container}:${artifactRoot}/VST3" $stagingRoot
        if ($LASTEXITCODE -ne 0) { throw "Could not copy VST3 artifacts from the direct build container." }
        & docker cp "${Container}:${artifactRoot}/Standalone" $stagingRoot
        if ($LASTEXITCODE -ne 0) { throw "Could not copy Standalone artifacts from the direct build container." }

        foreach ($name in @("VST3", "Standalone")) {
            $source = Join-Path $stagingRoot $name
            if (-not (Test-Path -LiteralPath $source)) { throw "Expected staged artifact directory was not created: $source" }
            $destinationPath = Join-Path $Destination $name
            if (Test-Path -LiteralPath $destinationPath) { Remove-Item -LiteralPath $destinationPath -Recurse -Force }
            Move-Item -LiteralPath $source -Destination $Destination -Force
        }
    }
    finally {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($null -eq (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker.exe is not available. Install/start Docker Desktop first."
}

$engine = (& docker info --format '{{.OSType}}' 2>$null | Out-String).Trim().ToLowerInvariant()
if ($engine -ne "windows") {
    throw "Docker is currently using the '$engine' engine. Switch Docker Desktop to Windows containers and rerun this script."
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $Root "dist\docker-windows-$($Config.ToLowerInvariant())"
}

Write-Host "Direct Windows-container build mode" -ForegroundColor Yellow
Write-Host "No Dockerfile is built and no toolchain/source image layer is committed." -ForegroundColor DarkGray
Write-Host "The validated LTSC 2022 base image gets one disposable writable container layer; toolchain, SDK, JUCE, build and CTest all run inside it." -ForegroundColor DarkGray

Write-Host "Pre-pulling validated base image: $BaseImage" -ForegroundColor Cyan
Invoke-DockerMonitored -Arguments @("pull", $BaseImage) -Activity "Windows base image pull"

$container = "retromatch-direct-$([guid]::NewGuid().ToString('N').Substring(0, 10))"
try {
    Write-Host "Creating disposable base container '$container' ..." -ForegroundColor Cyan
    & docker create --name $container $BaseImage powershell.exe -NoLogo -ExecutionPolicy Bypass -File C:\src\scripts\provision-container-windows.ps1 -Config $Config | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Could not create direct Windows build container from the validated base image." }

    Write-Host "Copying RetroMatch source into the stopped container..." -ForegroundColor Cyan
    & docker cp "$Root\." "${container}:C:/src"
    if ($LASTEXITCODE -ne 0) { throw "Could not copy RetroMatch source into the direct Windows build container." }

    Write-Host "Provisioning Build Tools + SDK + MinGit + JUCE, then compiling/testing..." -ForegroundColor Cyan
    Write-Host "This intentionally avoids docker build, so the hcsshim::ImportLayer step that is failing on this machine is not used." -ForegroundColor DarkGray
    Invoke-DockerMonitored -Arguments @("start", "--attach", $container) -Activity "Direct Windows container provision/build/test"

    Write-Host "Exporting VST3 and Standalone artifacts from the stopped container..." -ForegroundColor Cyan
    Export-Artifacts -Container $container -Destination $OutputDir -Configuration $Config
}
finally {
    & docker rm -f $container *> $null
}

Write-Host ""
Write-Host "Direct Windows container build complete." -ForegroundColor Green
Write-Host "Artifacts: $OutputDir"
Write-Host "Mode:      base-container writable layer only; no Docker image build/commit" -ForegroundColor DarkGray
