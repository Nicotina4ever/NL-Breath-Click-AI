REM SPDX-License-Identifier: MPL-2.0
@echo off
setlocal
set "trialPage=%~dp0..\build\rc3\listening_feedback\listening\ASCOLTA.html"
if not exist "%trialPage%" (
  echo Confronto d'ascolto locale non ancora preparato.
  echo Le registrazioni private non sono incluse nei pacchetti.
  pause
  exit /b 1
)
start "" "%trialPage%"
