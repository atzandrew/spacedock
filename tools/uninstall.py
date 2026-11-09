#!/usr/bin/env python3
"""Remove SpaceDock application files, retaining user preferences and pinned items."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def executable_field(path):
    value = str(path).replace('\\', '\\\\').replace('"', '\\"')
    value = value.replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    return f'Exec="{value}"'


def uninstall(prefix, config, bindir='bin', datadir='share'):
    binary = prefix / bindir / 'spacedock'
    desktop = prefix / datadir / 'applications/spacedock.desktop'
    autostart = config / 'autostart/spacedock.desktop'
    # Do not touch startup entries owned by another installation.
    if autostart.is_file():
        lines = autostart.read_text().splitlines()
        if executable_field(binary) in lines:
            autostart.unlink()
    paths = [binary, desktop, prefix / datadir / 'spacedock/tuning.json',
             prefix / datadir / 'spacedock/assets/MANIFEST.csv',
             prefix / bindir / 'spacedock-uninstall']
    for path in paths:
        if path.is_file() or path.is_symlink():
            path.unlink()
            print(f'Removed {path}')
    # Only prune our own empty directories; never recurse into user data.
    for path in [prefix / datadir / 'spacedock/assets', prefix / datadir / 'spacedock']:
        try:
            path.rmdir()
        except (FileNotFoundError, OSError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, help='Installation prefix (required when running from source).')
    args = parser.parse_args()
    prefix = args.prefix
    if prefix is None:
        if Path(__file__).name != 'spacedock-uninstall':
            parser.error('Supply --prefix when running the source uninstaller.')
        prefix = Path(__file__).absolute().parent.parent
    prefix = prefix.absolute()
    # CMake substitutes these for the installed copy; source usage uses standard paths.
    bindir, datadir = '@CMAKE_INSTALL_BINDIR@', '@CMAKE_INSTALL_DATADIR@'
    if bindir.startswith('@'):
        bindir, datadir = 'bin', 'share'
    config = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
    uninstall(prefix, config, bindir, datadir)
    refresh = shutil.which('kbuildsycoca6')
    if refresh:
        subprocess.run([refresh], check=True)
    print('SpaceDock uninstalled. Saved preferences, pinned items, and development launchers were retained.')
    print('A running SpaceDock will continue until you quit it.')


if __name__ == '__main__':
    main()
