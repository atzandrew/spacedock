"""Exercise graceful quits and termination signals in the real executable."""
import json
import os
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
        started = time.monotonic()
        process = subprocess.Popen([binary, '--smoke-test', '--tuning', str(path)],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            if termination is not None:
                time.sleep(0.4)
                process.send_signal(termination)
            output, errors = process.communicate(timeout=8)
            elapsed = time.monotonic() - started
            assert process.returncode == 0, (termination, process.returncode, errors.decode())
            assert b"TypeError: Cannot read property" not in errors, errors.decode()
            # The smoke test requests quit at 500 ms. Neither it nor a signal
            # should tear down the window before the 900 ms lowering finishes.
            assert elapsed >= 1.1, (termination, elapsed, errors.decode())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
