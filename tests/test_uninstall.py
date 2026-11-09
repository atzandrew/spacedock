"""Check removal ownership and preservation of settings in an isolated prefix."""
import importlib.util
from pathlib import Path
import tempfile
import sys

spec = importlib.util.spec_from_file_location('uninstall', sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='spacedock uninstall ') as folder:
    root = Path(folder)
    prefix, config = root / 'install', root / 'config'
    owned = ['bin/spacedock', 'bin/spacedock-uninstall', 'share/applications/spacedock.desktop',
             'share/spacedock/tuning.json', 'share/spacedock/assets/MANIFEST.csv']
    for name in owned:
        file = prefix / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text('installed file')
    config.mkdir()
    settings = config / 'spacedockrc'
    settings.write_text('user settings')
    startup = config / 'autostart/spacedock.desktop'
    startup.parent.mkdir()
    startup.write_text('[Desktop Entry]\nExec="/other/spacedock"\n')
    module.uninstall(prefix, config)
    assert startup.exists(), 'Removed another installation startup entry'
    assert settings.read_text() == 'user settings'
    assert all(not (prefix / name).exists() for name in owned)
    startup.write_text('[Desktop Entry]\n' + module.executable_field(prefix / 'bin/spacedock') + '\n')
    module.uninstall(prefix, config)
    assert not startup.exists()
    assert settings.read_text() == 'user settings'
print('Uninstall ownership, repeat removal, and retained preferences passed.')
