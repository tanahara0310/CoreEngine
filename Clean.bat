@echo off
rem Deletes build intermediates, caches and logs. The list lives in Project\Build\Scripts\Clean.ps1.
rem   Clean.bat              choose a level from the menu
rem   Clean.bat 2            delete up to level 2 (asks before deleting)
rem   Clean.bat 3 -DryRun    list what level 3 would delete without deleting
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Project\Build\Scripts\Clean.ps1" %*
set CLEAN_EXIT=%ERRORLEVEL%
pause
exit /b %CLEAN_EXIT%
