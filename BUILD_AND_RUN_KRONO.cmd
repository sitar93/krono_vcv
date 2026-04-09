@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "BUILD_PS1=%SCRIPT_DIR%scripts\build_windows.ps1"
set "RUN_PS1=%SCRIPT_DIR%scripts\run_rack.ps1"
set "CHECK_PS1=%SCRIPT_DIR%scripts\check_krono_install.ps1"
set "RACK_SDK=D:\Files\VCV\Rack-SDK"

if not exist "%BUILD_PS1%" (
  echo [krono] Missing script: %BUILD_PS1%
  pause
  exit /b 1
)

if not exist "%RUN_PS1%" (
  echo [krono] Missing script: %RUN_PS1%
  pause
  exit /b 1
)

if not exist "%CHECK_PS1%" (
  echo [krono] Missing script: %CHECK_PS1%
  pause
  exit /b 1
)

if not exist "%RACK_SDK%" (
  echo [krono] Rack SDK folder not found: %RACK_SDK%
  echo [krono] Edit this .cmd file and update RACK_SDK variable.
  pause
  exit /b 1
)

echo [krono] Build + install plugin...
powershell -NoProfile -ExecutionPolicy Bypass -File "%BUILD_PS1%" -RackDir "%RACK_SDK%" -InstallToRack
if errorlevel 1 (
  echo [krono] Build failed.
  pause
  exit /b 1
)

echo [krono] Verifying plugin installation...
powershell -NoProfile -ExecutionPolicy Bypass -File "%CHECK_PS1%"
if errorlevel 1 (
  echo [krono] Install check failed.
  pause
  exit /b 1
)

echo [krono] Launch Rack...
powershell -NoProfile -ExecutionPolicy Bypass -File "%RUN_PS1%" -WaitForExit -CheckAfterExit
if errorlevel 1 (
  echo [krono] Rack launch failed.
  pause
  exit /b 1
)

echo [krono] Done.
endlocal
exit /b 0
