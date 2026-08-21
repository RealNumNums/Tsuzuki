@echo off
REM Tsuzuki launcher.
REM
REM Runs the exe in place from build\Release - it needs the DLLs that sit
REM beside it there, so it cannot simply be copied out on its own.
REM
REM Double-click it for an interactive prompt, or pass arguments to script it.
REM Downloads default to %TEMP%\tsuzuki and are deleted after playback unless
REM you pass --keep.

setlocal
set "TSUZUKI_EXE=%~dp0buildRelease	suzuki.exe"

if not exist "%TSUZUKI_EXE%" (
  echo.
  echo   tsuzuki.exe was not found at "%TSUZUKI_EXE%".
  echo   Build it first:  cmake --build build --config Release
  echo.
  pause
  exit /b 1
)

REM Arguments given: pass straight through, no prompts, no pause.
if not "%~1"=="" (
  "%TSUZUKI_EXE%" --save-path "%TEMP%\tsuzuki" %*
  exit /b %ERRORLEVEL%
)

REM No arguments - assume a double-click and drive it interactively.
title Tsuzuki
echo.
echo   Tsuzuki - anime torrent streaming
echo   ---------------------------------
echo.
echo   Downloads go to %TEMP%\tsuzuki and are deleted after you finish
echo   watching. Needs mpv installed.
echo.

set "TITLE_INPUT="
set /p "TITLE_INPUT=  Anime title (blank to quit): "
if "%TITLE_INPUT%"=="" exit /b 0

set "EP_INPUT="
set /p "EP_INPUT=  Episode number (blank = choose from a list): "

set "EP_ARG="
if not "%EP_INPUT%"=="" set "EP_ARG=--episode %EP_INPUT%"

echo.
"%TSUZUKI_EXE%" --save-path "%TEMP%\tsuzuki" search "%TITLE_INPUT%" %EP_ARG%
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="2" (
  echo   Refused: that episode is not clearly in the torrent, so nothing
  echo   was played. Run again and leave the episode blank to pick a file
  echo   yourself.
)
echo.
pause
exit /b %RC%
