#!/usr/bin/env bash
set -euo pipefail

root="$HOME/vxwm"
state="$root/shell/workspace-state"
cursor_resources="$root/cursor.Xresources"
cursor_apply_source="$root/cursor-theme-apply.c"
cursor_apply="$root/cursor-theme-apply"
if [[ -r "$cursor_resources" ]]; then
	xrdb -merge "$cursor_resources"
	export XCURSOR_THEME="Bibata-Modern-Classic"
	export XCURSOR_SIZE=24
	if [[ ! -x "$cursor_apply" || "$cursor_apply_source" -nt "$cursor_apply" ]]; then
		cc -O2 -Wall -Wextra "$cursor_apply_source" -o "$cursor_apply" \
			$(pkg-config --cflags --libs xcursor x11)
	fi
	"$cursor_apply"
fi
watch_source="$root/shell/workspace-watch.c"
watcher="$root/shell/workspace-watch"
network_watch="$root/shell/network-watch.sh"
tray_source="$root/shell/tray-host.cpp"
tray_host="$root/shell/tray-host"
tray_menu_source="$root/shell/tray-menu.cpp"
tray_menu="$root/shell/tray-menu"
ui_source="$root/shell/main.cpp"
ui_app="$root/shell/vxwm-shell"
auth_source="$root/shell/password-check.c"
auth_helper="$root/shell/password-check"
color_picker_source="$root/shell/color-picker.c"
color_picker="$root/shell/color-picker"
qt_moc="$(pkg-config --variable=libdir Qt6Core)/qt6/moc"

if [[ ! -x "$watcher" || "$watch_source" -nt "$watcher" ]]; then
	cc -std=c99 -O2 -Wall -Wextra -o "$watcher" "$watch_source" -lX11
fi
if [[ ! -x "$tray_host" || "$tray_source" -nt "$tray_host" ]]; then
	"$qt_moc" "$tray_source" -o /tmp/tray-host.moc
	c++ -std=c++17 -O2 -fPIC -Wall -Wextra -I/tmp "$tray_source" \
		$(pkg-config --cflags --libs Qt6DBus Qt6Gui Qt6Core) -o "$tray_host"
	rm -f /tmp/tray-host.moc
fi
if pkg-config --exists gtk+-3.0; then
	if [[ ! -x "$tray_menu" || "$tray_menu_source" -nt "$tray_menu" ]]; then
		c++ -O2 -Wall -Wextra "$tray_menu_source" \
			$(pkg-config --cflags --libs gtk+-3.0) -o "$tray_menu"
	fi
fi
if [[ ! -x "$auth_helper" || "$auth_source" -nt "$auth_helper" ]]; then
	cc -std=c11 -O2 -Wall -Wextra -o "$auth_helper" "$auth_source" -lpam
fi
if [[ ! -x "$color_picker" || "$color_picker_source" -nt "$color_picker" ]]; then
	cc -std=c99 -O2 -Wall -Wextra -o "$color_picker" "$color_picker_source" -lX11
fi
if [[ ! -x "$ui_app" || "$ui_source" -nt "$ui_app" ]]; then
	"$qt_moc" "$ui_source" -o /tmp/main.moc
	c++ -std=c++17 -O2 -fPIC -I/tmp "$ui_source" \
		$(pkg-config --cflags --libs Qt6Quick Qt6Qml Qt6DBus Qt6Gui Qt6Core) \
		-lX11 \
		-o "$ui_app"
	rm -f /tmp/main.moc
fi
mkdir -p "$state"
session_id="${XDG_SESSION_ID:-}"
if [[ -n "$session_id" ]]; then
	session_key="$session_id:$(< /proc/sys/kernel/random/boot_id)"
	session_marker="$state/.session-id"
	previous_session_key=""
	if [[ -r "$session_marker" ]]; then
		IFS= read -r previous_session_key < "$session_marker" || true
	fi
	if [[ "$previous_session_key" != "$session_key" ]]; then
		rm -f "$state/lock-screen-open"
		printf '%s\n' "$session_key" > "$session_marker"
	fi
fi
rm -f "$state/.ready" "$state/.tray-ready" "$state/launcher-open" \
	"$state"/tray-*.png
"$tray_host" "$state" &
tray_host_pid=$!
for ((attempt = 0; attempt < 100; attempt++)); do
	[[ -f "$state/.tray-ready" ]] && break
	sleep 0.02
done
if [[ ! -f "$state/.tray-ready" ]]; then
	printf 'Tray listener is unavailable; continuing without bar tray icons.\n' >&2
	kill "$tray_host_pid" 2>/dev/null || true
	tray_host_pid=""
fi
tray_proxy_pid=""
if command -v xembedsniproxy >/dev/null 2>&1; then
	xembedsniproxy &
	tray_proxy_pid=$!
fi
"$watcher" "$state" &
watcher_pid=$!
bash "$network_watch" "$state" &
network_watch_pid=$!
for ((attempt = 0; attempt < 100; attempt++)); do
	[[ -f "$state/.ready" ]] && break
	sleep 0.02
done
if [[ ! -f "$state/.ready" ]]; then
	printf 'Could not initialize the workspace watcher.\n' >&2
	kill "$tray_host_pid" "$watcher_pid" "$network_watch_pid" 2>/dev/null || true
	[[ -z "$tray_proxy_pid" ]] || kill "$tray_proxy_pid" 2>/dev/null || true
	exit 1
fi

"$ui_app" "$root/shell/main.qml" >"$state/vxwm-shell.log" 2>&1 &
ui_pid=$!
cleanup() {
	local pids=("$ui_pid" "$watcher_pid" "$network_watch_pid")
	[[ -z "$tray_host_pid" ]] || pids+=("$tray_host_pid")
	kill "${pids[@]}" 2>/dev/null || true
	[[ -z "$tray_proxy_pid" ]] || kill "$tray_proxy_pid" 2>/dev/null || true
	rm -f "$state/.ready" "$state/.tray-ready"
}
trap cleanup EXIT

dimensions="$(xdpyinfo | awk '$1 == "dimensions:" && !found { print $2; found = 1 }')"
width="${dimensions%x*}"
height="${dimensions#*x}"

window_id() {
	xwininfo -root -tree |
		awk -v title="\"$1\"" 'index($0, title) && !found { print $1; found = 1 }'
}

for ((attempt = 0; attempt < 100; attempt++)); do
	left="$(window_id 'VXWM Edge Bar left')"
	top="$(window_id 'VXWM Edge Bar top')"
	right="$(window_id 'VXWM Edge Bar right')"
	bottom="$(window_id 'VXWM Edge Bar bottom')"
	clock="$(window_id 'VXWM Desktop Clock')"
	ram_widget="$(window_id 'VXWM Desktop RAM Usage')"
	performance_widget="$(window_id 'VXWM Desktop Performance')"
	if [[ -n "$left" && -n "$top" && -n "$right" && -n "$bottom" \
		&& -n "$clock" && -n "$ram_widget" && -n "$performance_widget" ]]; then
		break
	fi
	sleep 0.05
done

if [[ -z "${left:-}" || -z "${top:-}" || -z "${right:-}" \
	|| -z "${bottom:-}" || -z "${clock:-}" || -z "${ram_widget:-}" \
	|| -z "${performance_widget:-}" ]]; then
	printf 'Could not find all VXWM bars, the clock, or the desktop widgets.\n' >&2
	if [[ -s "$state/vxwm-shell.log" ]]; then
		cat "$state/vxwm-shell.log" >&2
	fi
	exit 1
fi

register_bar() {
	local id="$1"
	local strut="$2"
	local x="$3"
	local y="$4"
	local bar_width="$5"
	local bar_height="$6"
	xprop -id "$id" -f _NET_WM_STRUT_PARTIAL 32c \
		-set _NET_WM_STRUT_PARTIAL "$strut" >/dev/null
	xdotool windowunmap "$id"
	xdotool windowmove "$id" "$x" "$y"
	xdotool windowsize "$id" "$bar_width" "$bar_height"
	xdotool windowmap "$id"
}

register_bar "$left" "40, 0, 0, 0, 0, $((height - 1)), 0, 0, 0, 0, 0, 0" \
	0 0 40 "$height"
register_bar "$top" "0, 0, 4, 0, 0, 0, 0, 0, 40, $((width - 1)), 0, 0" \
	40 0 "$((width - 40))" 4
register_bar "$right" "0, 4, 0, 0, 0, 0, 4, $((height - 5)), 0, 0, 0, 0" \
	$((width - 4)) 4 4 "$((height - 8))"
register_bar "$bottom" "0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 40, $((width - 1))" \
	40 $((height - 4)) "$((width - 40))" 4

# Keep the unmanaged clock below clients while leaving it fixed to the screen.
xdotool windowlower "$clock"
xdotool windowlower "$ram_widget"
xdotool windowlower "$performance_widget"

wait "$ui_pid"
