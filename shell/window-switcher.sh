#!/usr/bin/env bash
set -euo pipefail

state="$HOME/vxwm/shell/workspace-state"
mkdir -p "$state"

case "${1:-}" in
	press)
		[[ -e "$state/lock-screen-open" ]] && exit 0
		if [[ ! -e "$state/window-switcher-open" ]]; then
			: > "$state/window-switcher-open"
		else
			rm -f "$state/window-switcher-open" \
				"$state/window-switcher-commit" \
				"$state"/window-switcher-next-*
		fi
		;;
	*)
		exit 2
		;;
esac
