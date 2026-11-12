#!/usr/bin/env bash
set -euo pipefail

color="$HOME/vxwm/shell/color-picker"
if [[ ! -x "$color" || "$HOME/vxwm/shell/color-picker.c" -nt "$color" ]]; then
	cc -std=c99 -O2 -Wall -Wextra "$HOME/vxwm/shell/color-picker.c" \
		-o "$color" -lX11
fi

value="$("$color")" || exit 0
printf '%s' "$value" | xclip -selection clipboard
notify-send --app-name="VXWM Color Picker" "Color copied" "$value"
