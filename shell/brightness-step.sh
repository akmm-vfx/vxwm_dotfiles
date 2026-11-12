#!/usr/bin/env bash
set -euo pipefail

direction="${1:?use up or down}"
state_dir="$HOME/vxwm/shell/workspace-state"
state_file="$state_dir/brightness-state"
mkdir -p "$state_dir"

exec 9>"$state_dir/brightness.lock"
flock -x 9

current="$(cat "$state_file" 2>/dev/null || true)"
if [[ ! "$current" =~ ^[0-9]+$ ]]; then
	current="$(ddcutil getvcp 10 --brief | awk '$1 == "VCP" && $2 == "10" { printf "%d", ($4 * 100 / $5) + 0.5 }')"
fi
[[ "$current" =~ ^[0-9]+$ ]] || exit 1

step="${2:-5}"
case "$direction" in
	up) target=$((current + step)) ;;
	down) target=$((current - step)) ;;
	*) printf 'Usage: %s up|down [step-percent]\n' "$0" >&2; exit 2 ;;
esac
(( target < 0 )) && target=0
(( target > 100 )) && target=100

printf '%s' "$target" > "$state_file"
if ! ddcutil setvcp 10 "$target" >/dev/null 2>&1; then
	actual="$(ddcutil getvcp 10 --brief 2>/dev/null | awk '$1 == "VCP" && $2 == "10" { printf "%d", ($4 * 100 / $5) + 0.5 }')"
	[[ "$actual" =~ ^[0-9]+$ ]] && printf '%s' "$actual" > "$state_file"
	exit 1
fi
