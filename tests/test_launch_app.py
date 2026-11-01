"""Exercise the real helper process and its generated startup-notifying launcher."""
from pathlib import Path
import os
import subprocess
import sys
import time

binary = Path(sys.argv[1])
launcher = Path(sys.argv[2])
assert os.access(launcher, os.X_OK), "Local desktop launcher must be executable for KDE to trust it"
entry = launcher.read_text()
assert 'StartupNotify=true\n' in entry
assert f'Exec="{binary}"\n' in entry
assert launcher.stem == 'spacedock-launch-test'
started = time.monotonic()
result = subprocess.run([str(binary), '--smoke-test'], capture_output=True, text=True, timeout=12)
assert result.returncode == 0, result.stderr
lines = result.stdout.splitlines()
assert len(lines) == 2 and lines[0] == 'loading', result.stdout
assert lines[1].startswith('ready '), result.stdout
ready_ms = int(lines[1].split()[1])
assert ready_ms >= 3000, f'Window rendered prematurely at {ready_ms}ms'
assert time.monotonic() - started >= 3
print(f'Real helper rendered its first window after {ready_ms}ms.')
