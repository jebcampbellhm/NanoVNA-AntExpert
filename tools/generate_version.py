"""Generate product/protocol versions and refresh Git provenance on every build."""
import argparse
from pathlib import Path
import re
import subprocess


def generate(root, output, version):
    upstream = (root / 'UPSTREAM_VERSION').read_text().strip()
    for value in (version, upstream):
        if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?', value):
            raise ValueError('Version must be major.minor.patch[-prerelease]')
    try:
        revision = subprocess.check_output(
            ['git', 'rev-parse', '--short=7', 'HEAD'], cwd=root,
            stderr=subprocess.DEVNULL, text=True).strip()
        if subprocess.run(['git', 'diff', '--quiet', '--ignore-cr-at-eol', 'HEAD', '--'], cwd=root).returncode:
            revision += '-dirty'
    except (OSError, subprocess.CalledProcessError):
        revision = 'unknown'
    content = ('#pragma once\n'
               f'#define NANOVNA_VERSION_STRING "{version}"\n'
               f'#define NANOVNA_PROTOCOL_VERSION_STRING "{upstream}"\n'
               f'#define ANTEXPERT_GIT_REVISION "{revision}"\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != content:
        output.write_bytes(content)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(Path(__file__).resolve().parents[1], args.output, args.version)
