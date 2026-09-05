@echo off
call "C:\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -winsdk=10.0.20348.0
if errorlevel 1 exit /b 1
"C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\build --target %*
exit /b %errorlevel%
