@echo off
title Akai Fire - install custom firmware
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\fire_tool.ps1" install
echo.
pause
