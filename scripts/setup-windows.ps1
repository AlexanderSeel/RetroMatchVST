param(
    [switch]$SkipVisualStudio
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget was not found. Install/update Microsoft App Installer, then run this script again."
}

Write-Host "Installing/updating Git and CMake..."
winget install --id Git.Git --source winget --accept-package-agreements --accept-source-agreements
winget install --id Kitware.CMake --source winget --accept-package-agreements --accept-source-agreements

if (-not $SkipVisualStudio) {
    Write-Host "Installing/updating Visual Studio Community with Desktop development with C++..."
    winget install --id Microsoft.VisualStudio.Community --source winget `
        --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --wait" `
        --accept-package-agreements --accept-source-agreements
}

Write-Host ""
Write-Host "Tool bootstrap finished. A reboot may be required after the Visual Studio installation."
Write-Host "Then open a NEW PowerShell window and run:"
Write-Host "  .\\scripts\\check-tools.ps1"
Write-Host "  .\\scripts\\build-windows.ps1"
