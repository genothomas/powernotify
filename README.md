# powernotify

Lightweight event-driven macOS battery notifier using IOKit and ntfy.

Sends one notification when battery is at or below 15% while on battery, then re-arms after it rises above 15%.

## Setup

```bash
git clone https://github.com/YOUR_USERNAME/powernotify.git
cd powernotify
./install.sh
```

Edit:

```text
~/.config/powernotify/config
```

```ini
NTFY_URL=https://ntfy.example.com/macos
NTFY_TOKEN=YOUR_NTFY_TOKEN
```

Protect it:

```bash
chmod 700 ~/.config/powernotify
chmod 600 ~/.config/powernotify/config
```

Run `./install.sh` again after editing the config.

## Check

```bash
pgrep -fl powernotify
launchctl print gui/$(id -u)/com.user.powernotify
```

Logs:

```bash
tail -f ~/Library/Logs/PowerNotify/powernotify.err
```

## Uninstall

```bash
./uninstall.sh
```
