#!/bin/sh
set -eu
bundle_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
destination="${1:-$HOME/.local/share/psyche}"
mkdir -p "$destination/runtime" "$destination/bin"
destination=$(CDPATH= cd -- "$destination" && pwd)
cp -R --remove-destination "$bundle_dir/lib" "$bundle_dir/plugins" "$bundle_dir/qml" "$bundle_dir/bin" "$bundle_dir/licenses" "$bundle_dir/fontconfig" "$destination/runtime/"
cp --remove-destination "$bundle_dir/psyche" "$destination/runtime/psyche"
cp "$bundle_dir/psyche.svg" "$destination/psyche.svg"
cat > "$destination/bin/psyche" <<'LAUNCHER'
#!/bin/sh
set -eu
installed_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "$installed_dir/runtime/psyche" "$@"
LAUNCHER
chmod 755 "$destination/bin/psyche" "$destination/runtime/psyche"
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
mkdir -p "$data_home/applications" "$data_home/icons/hicolor/scalable/apps"
cp "$bundle_dir/psyche.svg" "$data_home/icons/hicolor/scalable/apps/psyche.svg"
# Quote the desktop Exec argument independently of shell evaluation.
exec_path=$(printf '%s' "$destination/bin/psyche" | sed 's/\\/\\\\\\\\/g; s/"/\\\\\\"/g; s/`/\\\\`/g; s/\$/\\\\$/g; s/%/%%/g')
cat > "$data_home/applications/psyche.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=psyche
Comment=Import game entries into SLSsteam
Exec="$exec_path"
Icon=psyche
Terminal=false
Categories=Game;
StartupWMClass=psyche
DESKTOP
if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$data_home/applications" >/dev/null 2>&1 || :; fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -f -t "$data_home/icons/hicolor" >/dev/null 2>&1 || :; fi
printf 'Installed: %s/bin/psyche\nMenu shortcut: psyche\n' "$destination"
