@echo off
rem Runs install.ps1 regardless of the PowerShell execution policy on this PC.
rem Windows PowerShell must not inherit the module path of a parent PowerShell 7.
set "PSModulePath="
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
exit /b %ERRORLEVEL%
