@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "BUILD_PS1=%SCRIPT_DIR%scripts\build_windows.ps1"
set "RUN_PS1=%SCRIPT_DIR%scripts\run_rack.ps1"
set "CHECK_PS1=%SCRIPT_DIR%scripts\check_krono_install.ps1"
set "PREPARE_RELEASE_PS1=%SCRIPT_DIR%scripts\prepare_assets_from_dist.ps1"
set "RACK_SDK=D:\Files\VCV\Rack-SDK"

rem Optional: force Rack user folder if plugins are not in %%LOCALAPPDATA%%\Rack2
rem (e.g. portable install: set folder to the Rack2 directory next to Rack.exe)
rem set "RACK_USER_DIR=D:\Portable\Rack2Pro\Rack2"

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
powershell -NoProfile -ExecutionPolicy Bypass -File "%BUILD_PS1%" -RackDir "%RACK_SDK%" -InstallToRack -RackUserDir "%RACK_USER_DIR%"
if errorlevel 1 (
  echo [krono] Build failed.
  pause
  exit /b 1
)

if exist "%PREPARE_RELEASE_PS1%" (
  echo [krono] Refreshing release/ from dist/ ^(.vcvplugin + .sha256^)...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%PREPARE_RELEASE_PS1%" -UpdateReleaseNotes
  if errorlevel 1 (
    echo [krono] Warning: prepare_assets_from_dist failed ^(is dist\ present after build?^).
  )
) else (
  echo [krono] Warning: missing %PREPARE_RELEASE_PS1%
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
