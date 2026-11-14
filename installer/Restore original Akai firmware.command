#!/bin/sh
# macOS: double-click (the first time: right-click > Open). Linux: run  sh "Restore original Akai firmware.command"
cd "$(dirname "$0")" || exit 1
sh files/fire_tool.sh restore
printf '\nPress Enter to close this window. '; read -r _
