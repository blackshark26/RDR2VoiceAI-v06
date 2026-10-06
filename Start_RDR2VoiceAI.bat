@echo off
title RDR2 Voice AI v0.8 Interactive Roleplay
cd /d "%~dp0"
echo Starting RDR2 Voice AI v0.8 Interactive Roleplay...
echo.
if not exist "config.json" (
  echo ERROR: config.json was not found next to this file.
  pause
  exit /b 1
)
if not exist "RDR2VoiceBridge.exe" (
  echo ERROR: RDR2VoiceBridge.exe was not found.
  pause
  exit /b 1
)
start "RDR2 Voice AI Bridge" "%~dp0RDR2VoiceBridge.exe"
echo Bridge launched.
echo Start Red Dead Redemption 2 Story Mode.
echo Hold U to talk. Release U to receive the reply.
echo.
pause
