#!/bin/sh
# Run inside psyche-wayland-check with the release mounted at /bundle.
set -eu
export HOME=/tmp/psyche-home
export XDG_RUNTIME_DIR=/tmp/psyche-wayland
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CONFIG_HOME="$HOME/.config"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/fontconfig/conf.d"
chmod 700 "$XDG_RUNTIME_DIR"
# An incompatible host rule must never reach the bundled fontconfig parser.
printf '<fontconfig><invalid-host-rule/></fontconfig>\n' > "$XDG_CONFIG_HOME/fontconfig/conf.d/01-host.conf"
weston --backend=headless --renderer=pixman --socket=psyche-test --idle-time=0 > /tmp/weston.log 2>&1 &
compositor=$!
trap 'kill "$compositor" 2>/dev/null || :' EXIT
count=0
until test -S "$XDG_RUNTIME_DIR/psyche-test"; do
    kill -0 "$compositor"
    count=$((count + 1))
    test "$count" -lt 50
    sleep .1
done
/bundle/install.sh
export WAYLAND_DISPLAY=psyche-test
export QT_QPA_PLATFORM=wayland
export QSG_INFO=1
unset QT_QUICK_BACKEND
"$HOME/.local/share/psyche/bin/psyche" > /tmp/psyche.log 2>&1 &
app=$!
sleep 3
if ! kill -0 "$app"; then cat /tmp/psyche.log; exit 1; fi
/bundle/install.sh
kill -0 "$app"
kill "$app"
wait "$app" || :
cat /tmp/psyche.log
grep -q 'Loading backend software' /tmp/psyche.log
if grep -Ei 'Fontconfig warning|Fontconfig error|No decoration plugins|Failed to|invalid-host-rule' /tmp/psyche.log; then exit 1; fi
printf 'Wayland software startup and reinstall passed.\n'
