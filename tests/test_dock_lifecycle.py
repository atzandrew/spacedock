"""Exercise graceful quits and termination signals in the real executable."""
import json
import os
import select
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

binary, tuning_path = sys.argv[1:]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
with tempfile.TemporaryDirectory() as folder:
    tuning = json.loads(Path(tuning_path).read_text())
    tuning['autoHide'].update(enabled=False, slideDurationMs=900)
    path = Path(folder) / 'tuning.json'
    path.write_text(json.dumps(tuning))
    for termination in (None, signal.SIGINT, signal.SIGTERM):
        process = subprocess.Popen([binary, '--smoke-test', '--tuning', str(path)],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            ready, _, _ = select.select([process.stdout], [], [], 8)
            assert ready and process.stdout.readline() == b'spacedock smoke test ready\n', (
                'dock did not finish startup', process.poll())
            started = time.monotonic()
            if termination is not None:
                process.send_signal(termination)
            output, errors = process.communicate(timeout=8)
            elapsed = time.monotonic() - started
            assert process.returncode == 0, (termination, process.returncode, errors.decode())
            assert b"TypeError: Cannot read property" not in errors, errors.decode()
            # Neither a quit request nor a signal should tear down the window
            # before the 900 ms lowering animation has mostly completed.
            assert elapsed >= 0.75, (termination, elapsed, errors.decode())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
