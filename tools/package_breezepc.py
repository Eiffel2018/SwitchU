#!/usr/bin/env python3
"""Package the versioned BreezePC sources; never include local settings."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PC = ROOT / 'tools' / 'breezepc'
UPDATES = {
    'pcconnect_app.py': PC / 'pcconnect_app.py',
    'switchu_remote.py': ROOT / 'tools' / 'switchu_remote.py',
    'pc_game_history.py': ROOT / 'tools' / 'pc_game_history.py',
}
COMPANIONS = ('pcconnect.py', 'pcconnect_album.py', 'pcconnect_asm.py',
              'pcconnect_decomp.py', 'pcconnect_files.py', 'pcconnect_text.py',
              'pcconnect_app.bat')


def package(output):
    output.mkdir(parents=True, exist_ok=True)
    common = {'README.md': PC / 'README.md', 'LICENSE': ROOT / 'LICENSE',
              'REMOTE_CONTROL_ACCEPTANCE.md': PC / 'REMOTE_CONTROL_ACCEPTANCE.md'}
    full = dict(UPDATES, **{name: PC / name for name in COMPANIONS})
    for name, sources in (('BreezePC.zip', full), ('BreezePC-update.zip', UPDATES)):
        path = output / name
        payloads = {key: source.read_bytes() for key, source in {**common, **sources}.items()}
        for key, data in payloads.items():
            if key.endswith('.py'):
                compile(data, key, 'exec')
        with zipfile.ZipFile(path, 'w') as archive:
            for key, data in sorted(payloads.items()):
                entry = zipfile.ZipInfo(key, date_time=(1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, data)
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            assert set(archive.namelist()) == set(payloads)
            assert all(archive.read(key) == data for key, data in payloads.items())
        print(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    package(parser.parse_args().output)
