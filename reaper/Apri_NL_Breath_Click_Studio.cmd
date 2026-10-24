REM SPDX-License-Identifier: MPL-2.0
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_preview.ps1" -Visible
if errorlevel 1 pause
