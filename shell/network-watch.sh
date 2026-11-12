#!/usr/bin/env bash
set -euo pipefail

state_dir="${1:?state directory required}"
mkdir -p "$state_dir"

update_state() {
	local state
	state="$(nmcli -t -f TYPE,STATE device 2>/dev/null | awk -F: '
		$1 == "ethernet" && $2 == "connected" { wired = 1 }
		$1 == "wifi" && $2 == "connected" { wireless = 1 }
		END {
			if (wired) print "ethernet"
			else if (wireless) print "wifi"
			else print "offline"
		}')"
	[[ -n "$state" ]] || state=offline
	rm -f "$state_dir"/network-*
: > "$state_dir/network-$state"
}

update_state

coproc NM_MONITOR { nmcli monitor 2>/dev/null; }
monitor_pid=$NM_MONITOR_PID
cleanup() {
	kill "$monitor_pid" 2>/dev/null || true
	wait "$monitor_pid" 2>/dev/null || true
}
trap cleanup EXIT

while IFS= read -r -u "${NM_MONITOR[0]}" _; do
	update_state
done
