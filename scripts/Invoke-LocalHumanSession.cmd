@echo off
setlocal DisableDelayedExpansion
if "%~1"=="" goto usage
if "%~2"=="" goto usage
if not "%~3"=="" goto usage
rem PowerShell reads its telemetry preference at process startup.
rem Limit these settings to this launcher and its children.
set "POWERSHELL_TELEMETRY_OPTOUT=1"
set "POWERSHELL_UPDATECHECK=Off"
"%~1" -NoLogo -NoProfile -File "%~2"
exit /b %errorlevel%

:usage
echo Usage: Invoke-LocalHumanSession.cmd "PATH_TO_PWSH.EXE" "PATH_TO_PREPARED_LAUNCH.PS1" 1>&2
exit /b 2
