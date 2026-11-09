"""Exercise actual CMake installation, replacement and installed uninstaller."""
import os
from pathlib import Path
import subprocess
import select
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='spacedock install ') as temporary:
    root = Path(temporary)
    prefix = root / 'prefix'
    config = root / 'config'
    environment = dict(os.environ, XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(prefix / 'share'),
                       XDG_CACHE_HOME=str(root / 'cache'), QT_QPA_PLATFORM='offscreen')
    # Do not inherit a packaging staging root from the invoking environment.
    environment.pop('DESTDIR', None)
    startup = config / 'autostart/spacedock.desktop'
    startup.parent.mkdir(parents=True)
    startup_text = f'[Desktop Entry]\nExec="{prefix}/bin/spacedock"\nHidden=true\n'
    startup.write_text(startup_text)
    preferences = config / 'spacedockrc'
    preferences.write_text('saved pins and preferences')
    desktop = prefix / 'share/applications/spacedock.desktop'
    for iteration in range(2):
        if iteration:
            desktop.write_text('old development launcher')
        result = subprocess.run(['cmake', '--install', str(build), '--prefix', str(prefix)],
                                env=environment, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert f'Exec="{prefix}/bin/spacedock"' in desktop.read_text()
        assert startup.read_text() == startup_text, 'Update changed the login choice'
        assert preferences.read_text() == 'saved pins and preferences'
        assert (prefix / 'bin/spacedock-uninstall').is_file()
    result = subprocess.run([str(prefix / 'bin/spacedock'), '--smoke-test', '--version'], env=environment,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    # Installation must replace a binary even while the old process is using it.
    binary = prefix / 'bin/spacedock'
    process = subprocess.Popen([str(binary), '--smoke-test'], env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        ready, _, _ = select.select([process.stdout], [], [], 10)
        assert ready and process.stdout.readline() == b'spacedock smoke test ready\n'
        os.utime(binary, (1, 1))  # Force CMake to copy, instead of reporting up to date.
        update = subprocess.run(['cmake', '--install', str(build), '--prefix', str(prefix)],
                                env=environment, capture_output=True, text=True)
        assert update.returncode == 0, update.stderr
        assert process.poll() is None, 'Update interrupted the running application'
        _, errors = process.communicate(timeout=10)
        assert process.returncode == 0, errors.decode()
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
    result = subprocess.run([str(prefix / 'bin/spacedock-uninstall')], env=environment,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert not (prefix / 'bin/spacedock').exists()
    assert not desktop.exists()
    assert not startup.exists()
    assert not (prefix / 'bin/spacedock-uninstall').exists()
    assert preferences.read_text() == 'saved pins and preferences'
print('Staged installation, update, installed executable and uninstall passed.')
