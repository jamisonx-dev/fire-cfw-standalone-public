@echo off
title Akai Fire - restore original Akai firmware
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\fire_tool.ps1" restore
echo.
pause
