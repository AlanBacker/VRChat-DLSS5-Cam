#!/usr/bin/env bash
# Adds VRChat DLSS5 Cam to the application menu of the current user (a .desktop entry and the icon), and a
# `vrchat-dlss5-cam` command in ~/.local/bin. Run it again after moving the folder; `--remove` takes it out.
set -euo pipefail
APP_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
BIN="$HOME/.local/bin"
if [ "${1:-}" = "--remove" ]; then
    rm -f "$APPS/vrchat-dlss5-cam.desktop" "$ICONS/vrchat-dlss5-cam.png" "$BIN/vrchat-dlss5-cam"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
    echo "removed the menu entry and the command"
    exit 0
fi
[ -f "$APP_DIR/VRChatDLSS5Cam.exe" ] || { echo "run this from the unpacked VRChatDLSS5Cam folder" >&2; exit 1; }
chmod +x "$APP_DIR/vrchat-dlss5-cam"
mkdir -p "$APPS" "$ICONS" "$BIN"
cp -f "$APP_DIR/icon.png" "$ICONS/vrchat-dlss5-cam.png"
cat > "$APPS/vrchat-dlss5-cam.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=VRChat DLSS5 Cam
Comment=DLSS 5 neural rendering for pictures and videos (runs under Proton)
Exec="$APP_DIR/vrchat-dlss5-cam" %F
Icon=vrchat-dlss5-cam
Terminal=false
Categories=Graphics;AudioVideo;
MimeType=image/png;image/jpeg;image/gif;image/webp;video/mp4;video/quicktime;
StartupWMClass=vrchatdlss5cam.exe
DESKTOP
ln -sf "$APP_DIR/vrchat-dlss5-cam" "$BIN/vrchat-dlss5-cam"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
echo "installed: menu entry 'VRChat DLSS5 Cam' and the command $BIN/vrchat-dlss5-cam"
case ":$PATH:" in *":$BIN:"*) ;; *) echo "note: $BIN is not on your PATH; the menu entry works regardless" ;; esac
