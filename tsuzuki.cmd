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

if "%~1"=="" (
  echo.
  echo   Tsuzuki - anime torrent streaming
  echo.
  echo   tsuzuki search "frieren"                     browse releases, pick one
  echo   tsuzuki search "frieren" --episode 5         jump straight to episode 5
  echo   tsuzuki search "frieren" --res 1080          prefer 1080p
  echo   tsuzuki "magnet:?xt=urn:btih:..."            play a magnet directly
  echo.
  echo   --keep            don't delete after watching
  echo   --save-path DIR   download somewhere other than %%TEMP%%\tsuzuki
  echo.
  echo   Needs mpv installed. Exit code 2 means the episode you asked for
  echo   isn't clearly in that torrent - it refuses rather than playing the
  echo   wrong one. Re-run without --episode and pick a row.
  echo.
  exit /b 0
)

"%TSUZUKI_EXE%" --save-path "%TEMP%\tsuzuki" %*
exit /b %ERRORLEVEL%
