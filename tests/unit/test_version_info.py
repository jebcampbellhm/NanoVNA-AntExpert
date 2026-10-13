"""Check incremental version provenance and protocol/product separation."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('version', ROOT / 'tools/generate_version.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL).decode().strip()
    git('init')
    git('config', 'user.name', 'Version test')
    git('config', 'user.email', 'test@example.invalid')
    (root / 'UPSTREAM_VERSION').write_text('0.9.102\n')
    git('add', 'UPSTREAM_VERSION')
    git('commit', '-m', 'Initial')
    out = root / 'build/version_info.h'
    module.generate(root, out, '0.1.0')
    header = out.read_text()
    assert 'NANOVNA_VERSION_STRING "0.1.0"' in header
    assert 'NANOVNA_PROTOCOL_VERSION_STRING "0.9.102"' in header
    assert git('rev-parse', '--short=7', 'HEAD') in header and '-dirty' not in header
    # A Windows checkout's CRLF conversion is not a source modification.
    (root / 'UPSTREAM_VERSION').write_bytes(b'0.9.102\r\n')
    stamp = out.stat().st_mtime_ns
    module.generate(root, out, '0.1.0')
    assert out.stat().st_mtime_ns == stamp
    (root / 'UPSTREAM_VERSION').write_text('0.9.103\n')
    module.generate(root, out, '0.1.0')
    assert '-dirty' in out.read_text()
    git('add', 'UPSTREAM_VERSION')
    git('commit', '-m', 'Change')
    module.generate(root, out, '0.1.1')
    assert git('rev-parse', '--short=7', 'HEAD') in out.read_text()
    assert '-dirty' not in out.read_text() and '"0.1.1"' in out.read_text()
    try:
        module.generate(root, out, 'bad"version')
        raise AssertionError('Invalid version accepted')
    except ValueError:
        pass
assert 'NANOVNA_PROTOCOL_VERSION_STRING); }' in (ROOT / 'src/sys/shell_commands.c').read_text()
print('Version provenance, incremental refresh, and USB compatibility passed')
