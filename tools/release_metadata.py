"""Shared tag/file naming policy; emit GitHub Actions outputs during release preparation."""
import argparse
import json
from pathlib import Path
import re

TAG_PATTERN = re.compile(r'v(?P<version>[0-9]+\.[0-9]+\.[0-9]+)(?P<preview>-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?')


def package_version(tag, numeric_version=None):
    match = TAG_PATTERN.fullmatch(tag)
    if not match or (numeric_version is not None and match['version'] != numeric_version):
        raise ValueError('Tag must be v<VERSION>, optionally followed by a prerelease suffix')
    return tag[1:]


def package_names(matrix, version):
    return [f"Open-Q-{version}-{row['name']}.{row['extension']}" for row in matrix['include']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--version-file', type=Path, default=Path('VERSION'))
    parser.add_argument('--matrix', type=Path, default=Path('.github/release-matrix.json'))
    args = parser.parse_args()
    numeric = args.version_file.read_text(encoding='utf-8').strip()
    try:
        version = package_version(args.tag, numeric)
        if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', args.commit):
            raise ValueError('Expected a resolved Git commit hash')
    except ValueError as error:
        parser.error(str(error))
    matrix = json.loads(args.matrix.read_text(encoding='utf-8'))
    outputs = {
        'tag': args.tag,
        'version': numeric,
        'package_version': version,
        'prerelease': str('-' in version).lower(),
        'commit': args.commit,
        'matrix': json.dumps(matrix, separators=(',', ':')),
        'packages': json.dumps(package_names(matrix, version), separators=(',', ':')),
    }
    for name, value in outputs.items():
        print(f'{name}={value}')


if __name__ == '__main__':
    main()
