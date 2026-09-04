param(
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [switch]$InstallDocker,
    [switch]$NonInteractive
)

$ErrorActionPreference = "Stop"
$scriptRoot = $PSScriptRoot
$baseImage = "mcr.microsoft.com/dotnet/framework/runtime:4.8-windowsservercore-ltsc2022"
$legacyBuildImage = "retromatch-build-windows:1.0.0"
$sourceImage = "retromatch-source-windows:1.0.0"

function Invoke-Docker([string[]]$Arguments, [string]$Description, [switch]$AllowFailure) {
    Write-Host "$Description ..." -ForegroundColor Cyan
    & docker @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFailure) {
        throw "$Description failed (docker exit code $code)."
    }
    return $code
}

if ($null -eq (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker.exe is not available. Install/start Docker Desktop first."
}

$engine = (& docker info --format '{{.OSType}}' 2>$null | Out-String).Trim().ToLowerInvariant()
if ($engine -ne "windows") {
    throw "Docker is currently using the '$engine' engine. Switch Docker Desktop to Windows containers and rerun this script."
}

Write-Host "RetroMatch targeted Windows-layer recovery" -ForegroundColor Yellow
Write-Host "This does NOT run docker system prune and does not remove unrelated images." -ForegroundColor DarkGray
Write-Host "Current RetroMatch builds compile in a disposable container so compiler output is never committed as a Docker image layer." -ForegroundColor DarkGray

$staleContainers = @(& docker ps -aq --filter "name=retromatch-" 2>$null)
foreach ($container in $staleContainers) {
    if (-not [string]::IsNullOrWhiteSpace($container)) {
        & docker rm -f $container *> $null
    }
}

# Remove only RetroMatch image tags. The actual recovery build deliberately
# uses --no-cache below so a damaged intermediate Windows layer is not reused.
& docker image rm -f $legacyBuildImage *> $null
& docker image rm -f $sourceImage *> $null

Write-Host "Validating the LTSC 2022 base image by creating a container..." -ForegroundColor Cyan
& docker run --rm $baseImage cmd.exe /D /C ver
$baseHealthy = $LASTEXITCODE -eq 0

if (-not $baseHealthy) {
    Write-Warning "The LTSC 2022 base image could not create a container. Re-pulling only that base image."
    & docker image rm -f $baseImage *> $null
    Invoke-Docker -Arguments @("pull", $baseImage) -Description "Re-pulling Windows Server Core base image"

    & docker run --rm $baseImage cmd.exe /D /C ver
    if ($LASTEXITCODE -ne 0) {
        throw @"
The Windows base image is still unusable after a clean re-pull.
Docker Desktop's Windows container layer store is damaged below RetroMatch's own images.
Restart Docker Desktop and run this script once more. If hcsshim::ImportLayer still fails,
use Docker Desktop Troubleshoot > Clean up data as the final recovery step. That action is
intentionally not automated because it removes unrelated Docker state.
"@
    }
}

Write-Host "Base image container creation succeeded." -ForegroundColor Green
Write-Host "Rebuilding the Windows source/toolchain image without cached intermediate layers..." -ForegroundColor Yellow
Write-Host "This is intentionally slower than a normal build, but prevents a corrupt cached layer from being reused." -ForegroundColor DarkGray

$buildScript = Join-Path $scriptRoot "build-docker.ps1"
$params = @{
    Target = "Windows"
    Config = $Config
    NoCache = $true
}
if ($InstallDocker) { $params.InstallDocker = $true }
if ($NonInteractive) { $params.NonInteractive = $true }

try {
    & $buildScript @params
}
catch {
    throw @"
RetroMatch's clean Windows Docker rebuild failed after the base image passed validation.
If the Docker output immediately above still contains hcsshim::ImportLayer (0x3), the
remaining fault is Docker Desktop's Windows layer store rather than the RetroMatch source.
Restart Docker Desktop and retry this repair once. If it persists, use Docker Desktop
Troubleshoot > Clean up data, switch back to Windows containers, and rerun this script.
Original error: $($_.Exception.Message)
"@
}

if ($LASTEXITCODE -ne 0) {
    throw "RetroMatch recovery build failed (exit code $LASTEXITCODE)."
}

Write-Host "Windows Docker layer recovery and RetroMatch disposable-container build completed." -ForegroundColor Green
