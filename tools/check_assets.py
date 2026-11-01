#!/usr/bin/env python3
"""Check shipped-asset attribution and scan repository files for private hashes."""
import argparse
import csv
import hashlib
from pathlib import Path
import subprocess
import sys


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check(root, reference_dir=None, denylist=None):
    root = root.resolve()
    errors = []
    hashes = set()
    if denylist:
        for line in denylist.read_text().splitlines():
            value = line.partition('#')[0].strip()
            if value:
                if len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
                    errors.append('Invalid SHA-256 in denylist')
                else:
                    hashes.add(value)
    if reference_dir:
        if not reference_dir.is_dir():
            errors.append('Reference directory does not exist')
        else:
            hashes.update(digest(p) for p in reference_dir.rglob('*') if p.is_file())
    manifest = root / 'assets/MANIFEST.csv'
    listed = set()
    if not manifest.is_file():
        errors.append('Missing assets/MANIFEST.csv')
    else:
        with manifest.open(newline='') as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ['path', 'origin', 'license']:
                errors.append('Manifest must have path,origin,license columns')
            for row in reader:
                name = row.get('path', '')
                path = root / name
                if not name.startswith('assets/') or not path.resolve().is_relative_to(root / 'assets'):
                    errors.append(f'Invalid asset path: {name}')
                elif name in listed or not path.is_file() or path.is_symlink():
                    errors.append(f'Duplicate, missing, or symlinked asset: {name}')
                if not row.get('origin', '').strip() or not row.get('license', '').strip():
                    errors.append(f'Missing attribution: {name}')
                listed.add(name)
    # Local source artwork and icon themes are ignored rather than published.
    # Only skip ignored files when the root actually belongs to a Git worktree.
    tracked = subprocess.run(['git', '-C', str(root), 'ls-files', '-z'], capture_output=True)
    tracked_names = set(tracked.stdout.split(b'\0')) if tracked.returncode == 0 else set()
    asset_paths = list((root / 'assets').rglob('*'))
    ignored = subprocess.run(
        ['git', '-C', str(root), 'check-ignore', '--no-index', '-z', '--stdin'],
        input=b''.join(str(p.relative_to(root)).encode() + b'\0' for p in asset_paths),
        capture_output=True)
    ignored_paths = set(ignored.stdout.split(b'\0')) if ignored.returncode in (0, 1) else set()
    for path in asset_paths:
        name = str(path.relative_to(root)).encode()
        if name in ignored_paths:
            if name in tracked_names and path.is_file():
                errors.append(f'Ignored asset is still tracked: {path.relative_to(root)}')
            else:
                continue
        if path.is_symlink():
            errors.append(f'Symlink forbidden in assets: {path.relative_to(root)}')
        elif path.is_file() and path != manifest and path.relative_to(root).as_posix() not in listed:
            errors.append(f'Unlisted asset: {path.relative_to(root)}')
    if tracked.returncode == 0:
        paths = [root / p.decode() for p in tracked.stdout.split(b'\0') if p]
    else:
        # Supports source archives and the initial workspace before git init.
        paths = [p for p in root.rglob('*') if not any(
            part.startswith(('build', '.git', '.codex', '.agents', '.aws', '__pycache__'))
            or part in ('reference', 'references', 'comparisons') for part in p.relative_to(root).parts)]
    for path in paths:
        if path.relative_to(root).parts[0] in ('reference', 'references'):
            errors.append(f'Private reference path is tracked: {path.relative_to(root)}')
        if path.is_file() and not path.is_symlink() and digest(path) in hashes:
            errors.append(f'Private reference content detected: {path.relative_to(root)}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--reference-dir', type=Path)
    parser.add_argument('--denylist', type=Path)
    args = parser.parse_args()
    denylist = args.denylist or args.root / 'tools/reference-sha256.txt'
    errors = check(args.root, args.reference_dir, denylist)
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print('Asset policy passed.')
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
