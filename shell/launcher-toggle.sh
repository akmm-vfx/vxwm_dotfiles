#!/usr/bin/env bash
set -euo pipefail

state="$HOME/vxwm/shell/workspace-state/launcher-open"
state_dir="$(dirname "$state")"
runtime="${XDG_RUNTIME_DIR:-/tmp}"

mkdir -p "$state_dir"
exec 9>"$runtime/vxwm-launcher.lock"
flock -x 9

focus_launcher() {
	local window=""
	for ((attempt = 0; attempt < 50; attempt++)); do
		window="$(xdotool search --onlyvisible --name '^VXWM Application Launcher$' 2>/dev/null | head -1 || true)"
		if [[ -n "$window" ]]; then
			xdotool windowraise "$window" >/dev/null 2>&1 || true
			xdotool windowfocus --sync "$window" >/dev/null 2>&1 || true
			return
		fi
		sleep 0.02
	done
	return 1
}

if [[ "${1:-}" == "clipboard" ]]; then
	if xdotool search --onlyvisible --name '^VXWM Application Launcher$' >/dev/null 2>&1; then
		focus_launcher
	else
		if [[ -e "$state" ]]; then
			rm -f "$state"
			sleep 0.05
		fi
		: > "$state"
		focus_launcher || exit 1
	fi
	request="$state_dir/launcher-clipboard-request-$(date +%s%N)-$$"
	: > "$request"
	exit 0
fi

if xdotool search --onlyvisible --name '^VXWM Application Launcher$' >/dev/null 2>&1; then
	rm -f "$state"
elif [[ -e "$state" ]]; then
	rm -f "$state"
	sleep 0.05
	: > "$state"
	focus_launcher
else
	: > "$state"
	focus_launcher
fi
