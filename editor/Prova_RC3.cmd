REM SPDX-License-Identifier: MPL-2.0
@echo off
setlocal
set "APPDATA=%~dp0..\build\rc3\studio_profile"
if not exist "%APPDATA%" mkdir "%APPDATA%"
if not exist "%~dp0..\build\bin\Release\bc_studio.exe" (
  echo Compilare prima la build Release RC3.
  pause
  exit /b 1
)
start "" "%~dp0..\build\bin\Release\bc_studio.exe" %*
