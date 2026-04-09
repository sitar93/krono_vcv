@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "RACK_SDK=D:\Files\VCV\Rack-SDK"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\verify_krono_rack.ps1" -RackDir "%RACK_SDK%"
set "EC=%ERRORLEVEL%"
if not "%EC%"=="0" (
  echo [krono] VERIFY FAILED exit=%EC%
  pause
  exit /b %EC%
)
echo [krono] VERIFY OK
pause
exit /b 0
