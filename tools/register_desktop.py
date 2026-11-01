#!/usr/bin/env python3
"""Register this development build's launchers so KWin can identify its executable."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def executable_field(path):
    # Desktop Entry Exec quoting (not shell quoting); double percent signs are
    # literal percent signs rather than desktop field-code placeholders.
    value = str(path.resolve()).replace('\\', '\\\\').replace('"', '\\"')
    value = value.replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    return f'Exec="{value}"'


def install_entry(source, binary, destination):
    lines = source.read_text().splitlines()
    lines = [executable_field(binary) if line.startswith('Exec=') else line for line in lines]
    text = '\n'.join(lines) + '\n'
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Update just our named entry, and never follow an existing target symlink.
    with tempfile.NamedTemporaryFile(mode='w', dir=destination.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(text)
    try:
        temporary.chmod(0o644)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
    parser.add_argument('--data-home', type=Path, help='Override XDG_DATA_HOME (for isolated development/testing).')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    data_home = args.data_home or Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    entries = [('spacedock', 'spacedock'), ('spacedock-launch-test', 'spacedock-launch-test')]
    for desktop, binary in entries:
        if not (build / binary).is_file() or not (build / (desktop + '.desktop')).is_file():
            parser.error(f'Build the project first: missing {desktop} or {binary} in {build}')
    refresh = shutil.which('kbuildsycoca6')
    if not args.dry_run and not refresh:
        parser.error('kbuildsycoca6 is required to refresh KDE application registration')
    for desktop, binary in entries:
        destination = data_home / 'applications' / (desktop + '.desktop')
        print(f'{destination}: {executable_field(build / binary)}')
        if not args.dry_run:
            install_entry(build / (desktop + '.desktop'), build / binary, destination)
    if not args.dry_run:
        environment = os.environ.copy()
        environment['XDG_DATA_HOME'] = str(data_home)
        subprocess.run([refresh, '--noincremental'], env=environment, check=True)
        print('Desktop registration refreshed. Restart the dock to reconnect to KWin.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f'Registration failed: {error}', file=sys.stderr)
        sys.exit(1)
