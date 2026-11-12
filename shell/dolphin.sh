#!/usr/bin/env bash
set -euo pipefail

export XDG_CURRENT_DESKTOP=KDE
export KDE_SESSION_VERSION=6
export QT_QPA_PLATFORMTHEME=kde

exec /usr/sbin/dolphin "$@"
