#!/usr/bin/env bash
set -euo pipefail

state="$HOME/vxwm/shell/workspace-state/power-menu-open"
runtime="${XDG_RUNTIME_DIR:-/tmp}"

mkdir -p "$(dirname "$state")"
exec 9>"$runtime/vxwm-power-menu.lock"
flock -x 9

if [[ -e "$state" ]]; then
	rm -f "$state"
	exit 0
fi

: > "$state"
for ((attempt = 0; attempt < 50; attempt++)); do
	window="$(xdotool search --onlyvisible --name '^VXWM Power Menu$' 2>/dev/null | head -1 || true)"
	if [[ -n "$window" ]]; then
		xdotool windowraise "$window" >/dev/null 2>&1 || true
		xdotool windowfocus --sync "$window" >/dev/null 2>&1 || true
		exit 0
	fi
	sleep 0.02
done
