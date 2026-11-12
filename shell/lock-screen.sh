#!/usr/bin/env bash
set -euo pipefail

root="$HOME/vxwm"
state="$root/shell/workspace-state"
locked="$state/lock-screen-open"
background="$state/lock-screen-background.png"
temporary="$background.tmp-$$.png"
marker_tmp="$locked.tmp-$$"

mkdir -p "$state"
[[ -e "$locked" ]] && exit 0

if ! command -v magick >/dev/null 2>&1; then
	notify-send -u critical "VXWM lock screen unavailable" \
		"ImageMagick is required to capture the blurred lock background."
	exit 127
fi

cleanup() {
	rm -f -- "$temporary" "$marker_tmp"
}
trap cleanup EXIT

if ! magick import -silent -window root "$temporary"; then
	notify-send -u critical "VXWM lock screen failed" \
		"Could not capture the current desktop."
	exit 1
fi

if ! magick "$temporary" -blur 0x14 "$background"; then
	notify-send -u critical "VXWM lock screen failed" \
		"Could not blur the lock background."
	exit 1
fi

: > "$marker_tmp"
mv -f -- "$marker_tmp" "$locked"
