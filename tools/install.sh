#!/usr/bin/env bash
# Install or update from this checkout; dependency installation is distro-specific.
set -euo pipefail
spacedock_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
spacedock_prefix="$HOME/.local"
spacedock_build="$spacedock_root/build-install"
while (( $# )); do
    case "$1" in
        --prefix|--build-dir)
            if (( $# < 2 )); then printf 'Missing value for %s\n' "$1" >&2; exit 2; fi
            if [[ "$1" == --prefix ]]; then spacedock_prefix="$2"; else spacedock_build="$2"; fi
            shift 2 ;;
        --help)
            printf '%s\n' 'Usage: tools/install.sh [--prefix PATH] [--build-dir PATH]' \
                'Builds and installs the current checkout. Defaults: ~/.local and build-install.'
            exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
cmake -S "$spacedock_root" -B "$spacedock_build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$spacedock_build" --parallel 2
cmake --install "$spacedock_build" --prefix "$spacedock_prefix"
if command -v kbuildsycoca6 >/dev/null; then
    kbuildsycoca6
else
    printf '%s\n' 'KDE cache refresh unavailable; log out and back in to refresh application registration.' >&2
fi
printf '\nInstalled/updated SpaceDock in %s\n' "$spacedock_prefix"
printf '%s\n' 'Restart SpaceDock to use the update. Login startup and saved preferences were preserved.'
