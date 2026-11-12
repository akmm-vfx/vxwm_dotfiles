#!/usr/bin/env bash
set -euo pipefail

root="$HOME/vxwm"
state="$root/wallpaper.current"
pidfile="$root/wallpaper-video.pid"
update_palette() {
	local file="$1"
	local extension="${file##*.}"
	local source="$file"
	local frame="$root/shell/.matugen-frame-$$.png"
	local cache_file="$root/shell/workspace-state/matugen-wallpaper-cache"
	local signature
	local cached_signature=""
	local outputs=(Matugen.colors MatugenColors.qml accent-color kitty-matugen.conf flameshot.ini)
	local valid_cache=true
	mkdir -p -- "${cache_file%/*}"
	signature="$(printf '%s\n' "$file" "$extension"; stat -Lc '%n|%s|%Y' -- "$file" "$root/shell/matugen.toml" | sort)"
	signature="$(printf '%s' "$signature" | sha256sum | cut -d ' ' -f 1)"
	[[ -r "$cache_file" ]] && read -r cached_signature < "$cache_file" || true
	for output in "${outputs[@]}"; do
		[[ -s "$root/shell/$output" ]] || valid_cache=false
	done

	if [[ "$cached_signature" != "$signature" || "$valid_cache" != true ]]; then
		case "${extension,,}" in
			mp4|mkv|webm|mov|avi|m4v|ogv|mpg|mpeg)
				if ! ffmpeg -hide_banner -loglevel error -ss 1 -i "$file" -frames:v 1 \
					-vf 'scale=min(640\,iw):-2' -y "$frame"; then
					rm -f -- "$frame"
					ffmpeg -hide_banner -loglevel error -i "$file" -frames:v 1 \
						-vf 'scale=min(640\,iw):-2' -y "$frame"
				fi
				source="$frame"
				;;
		esac

		if ! matugen image "$source" --config "$root/shell/matugen.toml" \
			--prefer=less-saturation --mode=dark --quiet; then
			rm -f -- "$frame"
			return 1
		fi
		for output in "${outputs[@]}"; do
			[[ -s "$root/shell/$output" ]] || {
				rm -f -- "$frame"
				return 1
			}
		done
		printf '%s\n' "$signature" > "$cache_file.tmp"
		mv -f -- "$cache_file.tmp" "$cache_file"
	fi
	rm -f -- "$frame"
	local accent_hex
	read -r accent_hex < "$root/shell/accent-color"
	if [[ "$accent_hex" =~ ^#[[:xdigit:]]{6}$ ]]; then
		plasma-apply-colorscheme --accent-color "$accent_hex" Matugen >/dev/null
	fi
	if [[ -n "${DISPLAY:-}" ]] && command -v xprop >/dev/null 2>&1; then
		xprop -root -f _VXWM_THEME_RELOAD 8s -set _VXWM_THEME_RELOAD "$(date +%s%N)" >/dev/null 2>&1 || true
	fi
	reload_kitty_colors
}

reload_kitty_colors() {
	local socket
	local runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
	for socket in "$runtime"/kitty-*; do
		[[ -S "$socket" ]] || continue
		kitty @ --to "unix:$socket" set-colors --all --configured \
			"$root/shell/kitty-matugen.conf" >/dev/null 2>&1 || true
	done
}

refresh_shell_colors() {
	local pid pids
	local windows
	pids="$(pgrep -f -- "$root/shell/main.qml" || true)"
	[[ -n "$pids" ]] || return 0
	while IFS= read -r pid; do
		[[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null || true
	done <<< "$pids"
	for ((attempt = 0; attempt < 100; attempt++)); do
		pids="$(pgrep -f -- "$root/shell/main.qml" || true)"
		windows="$(xwininfo -root -tree | awk '/"VXWM Edge Bar / || /"VXWM Corner Connector / || /"VXWM Desktop Clock"/ { print; found = 1 }')"
		[[ -z "$pids" && -z "$windows" ]] && break
		sleep 0.05
	done
	setsid -f "$root/shell/start-ui.sh" </dev/null >/dev/null 2>&1
}

stop_video() {
	local pid command
	local pattern="^$HOME/.local/bin/xwinwrap .*--title=VXWM-Wallpaper"
	while IFS= read -r pid; do
		[[ "$pid" =~ ^[0-9]+$ ]] || continue
		command="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
		[[ "$command" == "$HOME/.local/bin/xwinwrap "* ]] || continue
		[[ "$command" == *"--title=VXWM-Wallpaper"* ]] || continue
		kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
	done < <(pgrep -f -- "$pattern" || true)
	sleep 0.2
	while IFS= read -r pid; do
		[[ "$pid" =~ ^[0-9]+$ ]] || continue
		command="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
		[[ "$command" == "$HOME/.local/bin/xwinwrap "* ]] || continue
		[[ "$command" == *"--title=VXWM-Wallpaper"* ]] || continue
		kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
	done < <(pgrep -f -- "$pattern" || true)
	rm -f -- "$pidfile"
}

apply_wallpaper() {
	local file="$1"
	local extension="${file##*.}"
	local temporary="$state.tmp"
	
	[[ -f "$file" ]] || return 1
	update_palette "$file"
	case "${extension,,}" in
		png|jpg|jpeg|webp|bmp|gif|tif|tiff)
			stop_video
			feh --no-fehbg --bg-fill -- "$file"
			;;
		mp4|mkv|webm|mov|avi|m4v|ogv|mpg|mpeg)
			stop_video
			setsid "$HOME/.local/bin/xwinwrap" -ov -fs -ni -nf -fdt -b -s -st -sp -- \
				mpv --no-config --wid=%WID --title=VXWM-Wallpaper --loop-file=inf --no-audio \
				--no-osc --osd-level=0 --no-input-default-bindings \
				--no-input-vo-keyboard --hwdec=auto-safe --panscan=1 \
				--really-quiet "$file" >/dev/null 2>&1 &
			printf '%s\n' "$!" > "$pidfile"
			;;
		*)
			printf 'Unsupported wallpaper format: %s\n' "$extension" >&2
			return 1
			;;
	esac

	printf '%s\n' "$file" > "$temporary"
	mv -f -- "$temporary" "$state"
	refresh_shell_colors
}

case "${1:---choose}" in
	--choose)
		if ! selected="$(XDG_CURRENT_DESKTOP=KDE QT_QPA_PLATFORMTHEME=kde kdialog --title 'Choose wallpaper' --getopenfilename "$HOME/Pictures/Wallpapers" 'Image and video files (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.mp4 *.mkv *.webm *.mov *.avi *.m4v *.ogv *.mpg *.mpeg)')"; then
			exit 0
		fi
		[[ -n "$selected" ]] || exit 0
		apply_wallpaper "$selected"
		;;
	--restore)
		[[ -s "$state" ]] || exit 0
		read -r selected < "$state"
		apply_wallpaper "$selected"
		;;
	*)
		printf 'Usage: %s [--choose|--restore]\n' "$0" >&2
		exit 2
		;;
esac
