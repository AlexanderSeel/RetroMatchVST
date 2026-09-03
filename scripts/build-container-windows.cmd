@echo off
setlocal EnableExtensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "WINSDK=10.0.20348.0"
set "VSDEVCMD=C:\BuildTools\Common7\Tools\VsDevCmd.bat"
set "CMAKE=C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CTEST=C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
set "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDKHEADER=%SDKROOT%\Include\%WINSDK%\um\Windows.h"
set "SDKLIB=%SDKROOT%\Lib\%WINSDK%\um\x64\kernel32.lib"
set "SDKRC=%SDKROOT%\bin\%WINSDK%\x64\rc.exe"

if not exist "%VSDEVCMD%" (
  echo ERROR: VsDevCmd.bat not found at %VSDEVCMD%
  exit /b 2
)
if not exist "%CMAKE%" (
  echo ERROR: cmake.exe not found at %CMAKE%
  exit /b 3
)
if not exist "%CTEST%" (
  echo ERROR: ctest.exe not found at %CTEST%
  exit /b 4
)
if not exist "%SDKHEADER%" (
  echo ERROR: Windows SDK %WINSDK% header payload is incomplete: %SDKHEADER%
  exit /b 9
)
if not exist "%SDKLIB%" (
  echo ERROR: Windows SDK %WINSDK% library payload is incomplete: %SDKLIB%
  exit /b 10
)
if not exist "%SDKRC%" (
  echo ERROR: Windows SDK %WINSDK% tool payload is incomplete: %SDKRC%
  exit /b 11
)

echo Windows SDK payload verified:
echo   %SDKHEADER%
echo   %SDKLIB%
echo   %SDKRC%

echo Initializing Visual Studio x64 build environment with Windows SDK %WINSDK%...
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 -winsdk=%WINSDK%
if errorlevel 1 exit /b %errorlevel%

where cl.exe
if errorlevel 1 (
  echo ERROR: cl.exe is not available after VsDevCmd initialization.
  exit /b 5
)
where nmake.exe
if errorlevel 1 (
  echo ERROR: nmake.exe is not available after VsDevCmd initialization.
  exit /b 6
)
where rc.exe
if errorlevel 1 (
  echo ERROR: rc.exe is not available after VsDevCmd initialization.
  exit /b 7
)

echo Selected Windows SDK: %WindowsSDKVersion%
echo Resource compiler path:
where rc.exe
echo %WindowsSDKVersion% | findstr /C:"%WINSDK%" >nul
if errorlevel 1 (
  echo ERROR: VsDevCmd selected Windows SDK %WindowsSDKVersion% instead of %WINSDK%.
  exit /b 8
)

echo Configuring RetroMatch Synth with MSVC/NMake (%CONFIG%)...
"%CMAKE%" -S C:\src -B C:\build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_SYSTEM_VERSION=%WINSDK% -DRETROMATCH_JUCE_DIR=C:\JUCE -DRETROMATCH_COPY_PLUGIN=OFF -DRETROMATCH_BUILD_TESTS=ON
if errorlevel 1 exit /b %errorlevel%

echo Building plug-in, standalone app and tests...
"%CMAKE%" --build C:\build --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone RetroMatchTests --parallel
if errorlevel 1 exit /b %errorlevel%

echo Running tests...
"%CTEST%" --test-dir C:\build --output-on-failure
if errorlevel 1 exit /b %errorlevel%

echo Windows container build completed successfully.
exit /b 0
