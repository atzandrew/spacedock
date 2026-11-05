"""Check real desktop registration and KDE cache refresh in isolated XDG paths."""
import configparser
import os
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(sys.argv[1]).resolve()
build = Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix='spacedock registration ') as temporary:
    root = Path(temporary)
    data = root / 'data'
    environment = os.environ.copy()
    environment.update(XDG_DATA_HOME=str(data), XDG_CACHE_HOME=str(root / 'cache'),
                       XDG_CONFIG_HOME=str(root / 'config'), QT_QPA_PLATFORM='offscreen')
    command = [sys.executable, str(script), '--build-dir', str(build), '--data-home', str(data)]
    result = subprocess.run(command + ['--dry-run'], env=environment, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    assert not data.exists(), 'Dry run modified the registration directory'
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    for desktop, binary in [('spacedock', 'spacedock'),
                            ('spacedock-launch-test', 'spacedock-launch-test')]:
        entry = configparser.ConfigParser(interpolation=None)
        entry.optionxform = str
        entry.read(data / 'applications' / (desktop + '.desktop'))
        settings = entry['Desktop Entry']
        assert settings['Exec'] == f'"{(build / binary).resolve()}"'
        if desktop == 'spacedock':
            assert settings['Comment[fr]'] == 'Un dock de style classique pour Plasma Wayland'
            for locale in ('de', 'es', 'it', 'ja', 'pl', 'pt_BR', 'ru', 'uk', 'zh_CN'):
                assert settings[f'Comment[{locale}]'], f'Missing desktop translation: {locale}'
            assert set(settings['X-KDE-Wayland-Interfaces'].split(',')) == {
                'org_kde_plasma_window_management', 'org_kde_plasma_activation_feedback'}
        else:
            assert settings['StartupNotify'] == 'true'
    assert list((root / 'cache').glob('ksycoca6*')), 'KDE service cache was not generated'
    # The one-step development runner must register first, then exec the real
    # binary with its supplied arguments. Keep all registrations/cache isolated.
    runner = script.parent / 'run-dev.sh'
    result = subprocess.run([str(runner), '--smoke-test'], env=environment,
                            capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
print('Build launchers, isolated KDE cache, and registration-before-exec runner passed.')
