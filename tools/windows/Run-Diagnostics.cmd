@echo off
setlocal
cd /d "%~dp0"
set "PATH=%~dp0;%SystemRoot%\System32;%SystemRoot%"
set "QT_PLUGIN_PATH="
set "QT_QPA_PLATFORM_PLUGIN_PATH="
set "QT_QPA_PLATFORM="
set "QML_IMPORT_PATH="
set "QML2_IMPORT_PATH="
set "QSG_RHI_BACKEND="
set "QSG_RHI_PREFER_SOFTWARE_RENDERER="
"%~dp0Drawborad.exe" --diagnostics "%~dp0user-device-diagnostics.json"
if errorlevel 1 goto failed
"%~dp0Drawborad.exe" --smoke --warp --diagnostics "%~dp0user-startup-diagnostics.json"
if errorlevel 1 goto failed
echo Device and native startup diagnostics written beside Drawborad.exe.
echo These checks do not establish model inference or painting fidelity.
pause
exit /b 0
:failed
echo Diagnostics failed. Retain any JSON files and the exact visible error.
pause
exit /b 1
