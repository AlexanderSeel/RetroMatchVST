$tools = @("cmake", "git")
foreach ($tool in $tools) {
    $cmd = Get-Command $tool -ErrorAction SilentlyContinue
    if ($cmd) { Write-Host "[OK] $tool -> $($cmd.Source)" } else { Write-Host "[MISSING] $tool" }
}
Write-Host ""
Write-Host "CMake generators containing Visual Studio:"
cmake --help 2>$null | Select-String "Visual Studio"
