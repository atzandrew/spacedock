#!/usr/bin/env bash
set -euo pipefail
# Register before opening the Wayland connection: KWin caches permissions per client.
spacedock_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
python3 "$spacedock_root/tools/register_desktop.py"
exec "$spacedock_root/build/spacedock" --development "$@"
