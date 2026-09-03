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

rem Avoid parenthesized IF blocks for SDK paths. Paths under Program Files (x86)
rem contain parentheses that cmd.exe can misparse when expanded inside a block.
if not exist "%VSDEVCMD%" goto :missing_vsdevcmd
if not exist "%CMAKE%" goto :missing_cmake
if not exist "%CTEST%" goto :missing_ctest
if not exist "%SDKHEADER%" goto :missing_sdk_header
if not exist "%SDKLIB%" goto :missing_sdk_lib
if not exist "%SDKRC%" goto :missing_sdk_rc

echo Windows SDK payload verified:
echo   %SDKHEADER%
echo   %SDKLIB%
echo   %SDKRC%

echo Initializing Visual Studio x64 build environment with Windows SDK %WINSDK%...
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 -winsdk=%WINSDK%
if errorlevel 1 goto :vsdevcmd_failed

where cl.exe
if errorlevel 1 goto :missing_cl
where nmake.exe
if errorlevel 1 goto :missing_nmake
where rc.exe
if errorlevel 1 goto :missing_rc

echo Selected Windows SDK: %WindowsSDKVersion%
echo Resource compiler path:
where rc.exe
echo %WindowsSDKVersion% | findstr /C:"%WINSDK%" >nul
if errorlevel 1 goto :wrong_sdk

echo Configuring RetroMatch Synth with MSVC/NMake (%CONFIG%)...
"%CMAKE%" -S C:\src -B C:\build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_SYSTEM_VERSION=%WINSDK% -DRETROMATCH_JUCE_DIR=C:\JUCE -DRETROMATCH_COPY_PLUGIN=OFF -DRETROMATCH_BUILD_TESTS=ON
if errorlevel 1 goto :configure_failed

echo Building plug-in, standalone app and tests...
"%CMAKE%" --build C:\build --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone RetroMatchTests --parallel
if errorlevel 1 goto :build_failed

echo Running tests...
"%CTEST%" --test-dir C:\build --output-on-failure
if errorlevel 1 goto :tests_failed

echo Windows container build completed successfully.
exit /b 0

:missing_vsdevcmd
echo ERROR: VsDevCmd.bat not found at %VSDEVCMD%
exit /b 2

:missing_cmake
echo ERROR: cmake.exe not found at %CMAKE%
exit /b 3

:missing_ctest
echo ERROR: ctest.exe not found at %CTEST%
exit /b 4

:missing_cl
echo ERROR: cl.exe is not available after VsDevCmd initialization.
exit /b 5

:missing_nmake
echo ERROR: nmake.exe is not available after VsDevCmd initialization.
exit /b 6

:missing_rc
echo ERROR: rc.exe is not available after VsDevCmd initialization.
exit /b 7

:wrong_sdk
echo ERROR: VsDevCmd selected Windows SDK %WindowsSDKVersion% instead of %WINSDK%.
exit /b 8

:missing_sdk_header
echo ERROR: Windows SDK %WINSDK% header payload is incomplete.
echo Missing: %SDKHEADER%
exit /b 9

:missing_sdk_lib
echo ERROR: Windows SDK %WINSDK% library payload is incomplete.
echo Missing: %SDKLIB%
exit /b 10

:missing_sdk_rc
echo ERROR: Windows SDK %WINSDK% tool payload is incomplete.
echo Missing: %SDKRC%
exit /b 11

:vsdevcmd_failed
echo ERROR: VsDevCmd.bat failed while selecting Windows SDK %WINSDK%.
exit /b 12

:configure_failed
echo ERROR: CMake configure failed.
exit /b 13

:build_failed
echo ERROR: CMake build failed.
exit /b 14

:tests_failed
echo ERROR: CTest failed.
exit /b 15
