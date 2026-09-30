#!/bin/bash
set -euo pipefail

APP=powernotify
PLIST=com.user.powernotify
BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="$HOME/.config/$APP"
PLIST_DIR="$HOME/Library/LaunchAgents"
LOG_DIR="$HOME/Library/Logs/PowerNotify"

make
mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$PLIST_DIR" "$LOG_DIR"
install -m 755 "$APP" "$BIN_DIR/$APP"
chmod 700 "$CONFIG_DIR"

if [[ ! -f "$CONFIG_DIR/config" ]]; then
    cp config.example "$CONFIG_DIR/config"
    chmod 600 "$CONFIG_DIR/config"
    echo "Created $CONFIG_DIR/config"
    echo "Edit it with your ntfy URL and token, then rerun: $0"
    exit 0
fi

cat > "$PLIST_DIR/$PLIST.plist" <<EOF2
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$PLIST</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BIN_DIR/$APP</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ProcessType</key>
    <string>Background</string>
    <key>StandardOutPath</key>
    <string>$LOG_DIR/powernotify.log</string>
    <key>StandardErrorPath</key>
    <string>$LOG_DIR/powernotify.err</string>
</dict>
</plist>
EOF2

plutil -lint "$PLIST_DIR/$PLIST.plist" >/dev/null
launchctl bootout "gui/$(id -u)/$PLIST" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST_DIR/$PLIST.plist"

echo "Installed: $APP"
echo "Config:    $CONFIG_DIR/config"
echo "Status:    launchctl print gui/$(id -u)/$PLIST"
