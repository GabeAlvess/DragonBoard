@echo off
setlocal
if "%~1"=="" (
  echo Drag an OBJ, FBX, folder, or ZIP onto this file.
  echo.
  set /p INPUT=Input path: 
) else (
  set "INPUT=%~1"
)
python "%~dp0convert.py" "%INPUT%"
echo.
if errorlevel 1 (
  echo Conversion failed.
) else (
  echo Conversion completed.
)
pause
