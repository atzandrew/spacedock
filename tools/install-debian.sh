#!/usr/bin/env bash
set -euo pipefail

# Install for the invoking user; only apt needs elevated privileges.
if (( EUID == 0 )); then
    printf '%s\n' 'Run this script as your desktop user, not with sudo.' >&2
    exit 1
fi
if ! command -v apt-get >/dev/null; then
    printf '%s\n' 'This installer requires Debian apt-get (tested package names: Debian 13/Trixie).' >&2
    exit 1
fi

spacedock_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
spacedock_build="$spacedock_root/build-install"
spacedock_prefix="$HOME/.local"

sudo apt-get update
sudo apt-get install \
    build-essential cmake python3 \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
    libkf6config-dev libkf6coreaddons-dev libkf6service-dev libkf6widgetsaddons-dev \
    plasma-workspace plasma-desktop qt6-wayland layer-shell-qt \
    qml6-module-org-kde-layershell qml6-module-org-kde-iconthemes \
    qml6-module-qtqml-workerscript qml6-module-qtquick \
    qml6-module-qtquick-controls qml6-module-qtquick-dialogs \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-window

"$spacedock_root/tools/install.sh" --build-dir "$spacedock_build" --prefix "$spacedock_prefix"

case ":$PATH:" in
    *":$spacedock_prefix/bin:"*)
        printf '%s\n' 'Start it from the application menu or run spacedock in a Plasma Wayland session.'
        ;;
    *)
        printf 'Start it from the application menu or run %s/bin/spacedock in a Plasma Wayland session.\n' "$spacedock_prefix"
        printf 'To use the spacedock command, add %s/bin to your PATH (see README.md).\n' "$spacedock_prefix"
        ;;
esac
