param(
    [ValidateSet("Debug", "Release")][string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$JuiceTag = "9.0.1"
$WinSdkVersion = "10.0.20348.0"
$MinGitVersion = "2.55.0.5"
$MinGitRelease = "v2.55.0.windows.5"
$MinGitSha256 = "56D7B226B7693196CFC71FEF26568F536C4A021AB6C37FF2DB4287BED908E96E"

function Invoke-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Name
    )

    Write-Host "Installing $Name ..." -ForegroundColor Cyan
    $process = Start-Process $Path -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "$Name installer failed with exit code $($process.ExitCode)."
    }
}

New-Item -ItemType Directory -Force C:\TEMP | Out-Null

$cl = Get-ChildItem 'C:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending | Select-Object -First 1
if (-not $cl) {
    $installer = 'C:\TEMP\vs_buildtools.exe'
    Invoke-WebRequest -UseBasicParsing https://aka.ms/vs/17/release/vs_buildtools.exe -OutFile $installer
    Invoke-Installer -Path $installer -Name 'Visual Studio 2022 Build Tools' -Arguments @(
        '--quiet','--wait','--norestart','--nocache',
        '--installPath','C:\BuildTools',
        '--add','Microsoft.VisualStudio.Workload.VCTools',
        '--add','Microsoft.VisualStudio.Component.VC.CMake.Project',
        '--includeRecommended'
    )
    Remove-Item $installer -Force

    $cl = Get-ChildItem 'C:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' -ErrorAction SilentlyContinue |
          Sort-Object FullName -Descending | Select-Object -First 1
}

if (-not $cl) {
    $installer = 'C:\TEMP\vs_buildtools.exe'
    Write-Host 'MSVC x64/x86 compiler missing after workload install; adding explicit compiler component...' -ForegroundColor Yellow
    Invoke-WebRequest -UseBasicParsing https://aka.ms/vs/17/release/vs_buildtools.exe -OutFile $installer
    Invoke-Installer -Path $installer -Name 'MSVC x64/x86 compiler component' -Arguments @(
        '--quiet','--wait','--norestart','--nocache',
        '--installPath','C:\BuildTools',
        '--add','Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    )
    Remove-Item $installer -Force

    $cl = Get-ChildItem 'C:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' -ErrorAction SilentlyContinue |
          Sort-Object FullName -Descending | Select-Object -First 1
}

if (-not $cl) {
    throw 'MSVC x64 compiler (cl.exe) is still missing after Build Tools installation.'
}

$vcTargets = 'C:\BuildTools\MSBuild\Microsoft\VC\v170\Microsoft.Cpp.Default.props'
if (-not (Test-Path $vcTargets)) {
    throw 'Visual C++ MSBuild targets are missing.'
}
Write-Host "MSVC compiler ready: $($cl.FullName)" -ForegroundColor Green

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkHeader = Join-Path $sdkRoot "Include\$WinSdkVersion\um\Windows.h"
$sdkLib = Join-Path $sdkRoot "Lib\$WinSdkVersion\um\x64\kernel32.lib"
$sdkRc = Join-Path $sdkRoot "bin\$WinSdkVersion\x64\rc.exe"
if (-not ((Test-Path $sdkHeader) -and (Test-Path $sdkLib) -and (Test-Path $sdkRc))) {
    $installer = 'C:\TEMP\winsdksetup.exe'
    Write-Host 'Installing complete serviced Windows SDK 20348 desktop C++ payload...' -ForegroundColor Cyan
    Invoke-WebRequest -UseBasicParsing https://go.microsoft.com/fwlink/?linkid=2331862 -OutFile $installer
    Invoke-Installer -Path $installer -Name 'Windows SDK 20348' -Arguments @(
        '/features','OptionId.DesktopCPPx64','OptionId.SigningTools','/quiet','/norestart'
    )
    Remove-Item $installer -Force
}

foreach ($path in @($sdkHeader, $sdkLib, $sdkRc)) {
    if (-not (Test-Path $path)) {
        throw "Windows SDK 20348 payload is incomplete: $path"
    }
}
Write-Host "Windows SDK 20348 ready: $sdkHeader $sdkLib $sdkRc" -ForegroundColor Green

$gitExe = 'C:\Git\cmd\git.exe'
if (-not (Test-Path $gitExe)) {
    $archive = 'C:\TEMP\mingit.zip'
    Invoke-WebRequest -UseBasicParsing "https://github.com/git-for-windows/git/releases/download/$MinGitRelease/MinGit-$MinGitVersion-64-bit.zip" -OutFile $archive
    $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash
    if ($actual -ne $MinGitSha256) {
        throw "MinGit checksum mismatch. Expected $MinGitSha256, got $actual."
    }
    Expand-Archive $archive -DestinationPath C:\Git
    Remove-Item $archive -Force
}

& $gitExe --version
if ($LASTEXITCODE -ne 0) { throw 'MinGit validation failed.' }

if (-not (Test-Path 'C:\JUCE\CMakeLists.txt')) {
    & $gitExe clone --depth 1 --branch $JuiceTag https://github.com/juce-framework/JUCE.git C:\JUCE
    if ($LASTEXITCODE -ne 0) { throw 'JUCE clone failed.' }
}
if (-not (Test-Path 'C:\JUCE\CMakeLists.txt')) {
    throw 'JUCE clone completed without C:\JUCE\CMakeLists.txt.'
}

Write-Host 'Toolchain provisioned inside disposable container. Starting RetroMatch configure/build/test...' -ForegroundColor Cyan
& C:\Windows\System32\cmd.exe /D /C C:\src\scripts\build-container-windows.cmd $Config
if ($LASTEXITCODE -ne 0) {
    throw "RetroMatch Windows container build failed with exit code $LASTEXITCODE."
}
