#!/bin/bash
set -euo pipefail

APP=powernotify
PLIST=com.user.powernotify

launchctl bootout "gui/$(id -u)/$PLIST" 2>/dev/null || true
rm -f "$HOME/Library/LaunchAgents/$PLIST.plist"
rm -f "$HOME/.local/bin/$APP"

echo "Removed $APP binary and LaunchAgent."
echo "Config/state were kept."
