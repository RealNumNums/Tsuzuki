@echo off
REM Launcher for Tsuzuki.
REM
REM The exe needs the DLLs that sit beside it in build\Release, so this calls
REM it in place rather than copying it out.
REM
REM Default save path is %TEMP%\tsuzuki - downloads are disposable and get
REM removed after playback unless you pass --keep. Passing your own
REM --save-path after the other arguments overrides this default.

setlocal
set "TSUZUKI_EXE=%~dp0build\Release\tsuzuki.exe"

if not exist "%TSUZUKI_EXE%" (
  echo tsuzuki.exe not found at "%TSUZUKI_EXE%"
  echo Build it first:  cmake --build build --config Release
  exit /b 1
)

"%TSUZUKI_EXE%" --save-path "%TEMP%\tsuzuki" %*
exit /b %ERRORLEVEL%
