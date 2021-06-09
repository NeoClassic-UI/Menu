@echo off
cd %~dp0

call __MakeFinalAllLanguages.bat ARM64
if ERRORLEVEL 1 exit /b 1

exit /b 0
