"""Run with python3 -m unittest discover -s tools/tests.

Checks the release matrix contract and executes the workflow's real checksum step
against complete, missing and empty download fixtures.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]


def checksum_script():
    workflow = (ROOT / '.github/workflows/release.yml').read_text()
    step = workflow.split('      - name: Verify packages and create checksums\n', 1)[1]
    body = step.split('        run: |\n', 1)[1].split('      - name:', 1)[0]
    return textwrap.dedent(body)


class ReleaseConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.rows = json.loads((ROOT / '.github/release-matrix.json').read_text())['include']

    def test_everyday_ci_keeps_its_original_platform_coverage(self):
        workflow = (ROOT / '.github/workflows/build_and_test.yml').read_text()
        fallback = re.search(r"fromJSON\(inputs\.build_matrix \|\| '(.*?)'\)", workflow, re.S)
        self.assertIsNotNone(fallback)
        rows = json.loads(fallback.group(1))['include']
        self.assertEqual([row['name'] for row in rows], ['Linux', 'macOS', 'Windows'])
        self.assertTrue(all(row['simd'] == 'DEFAULT' for row in rows))
        self.assertIn('arm64;x86_64', rows[1]['extra-flags'])

    def test_release_variants_are_unique_and_complete(self):
        expected = {
            'Linux-x86-64', 'Linux-x86-64-AVX2', 'Linux-arm64',
            'Windows-x86-64', 'Windows-x86-64-AVX2', 'Windows-arm64',
            'macOS-x86-64', 'macOS-arm64',
        }
        self.assertEqual({row['name'] for row in self.rows}, expected)
        self.assertEqual(len(self.rows), len(expected))

    def test_variants_select_matching_architecture_and_native_validation(self):
        for row in self.rows:
            with self.subTest(variant=row['name']):
                arm = row['name'].endswith('-arm64')
                windows = row['name'].startswith('Windows-')
                mac = row['name'].startswith('macOS-')
                self.assertEqual(row['arch'], 'arm64' if arm else 'x64')
                self.assertEqual(row['simd'], 'NEON' if arm else ('AVX2' if row['name'].endswith('-AVX2') else 'SSE2'))
                self.assertEqual(row['extension'], 'exe' if windows else 'zip')
                self.assertEqual(row['native-pluginval'], arm and not mac)
                if mac:
                    self.assertIn('-DCMAKE_OSX_ARCHITECTURES=' + ('arm64' if arm else 'x86_64'), row['extra-flags'])
                    self.assertNotIn(';', row['extra-flags'])
                if arm and not mac:
                    self.assertIn('arm', row['os'])
                    self.assertIn('pluginval-build', row['pluginval-binary'])

    def test_checksums_require_every_variant_and_cover_all_packages(self):
        packages = ['Open-Q-0.1.0-' + row['name'] + '.' + row['extension'] for row in self.rows]
        with tempfile.TemporaryDirectory(prefix='openq-release-check-') as directory:
            root = Path(directory)
            env = {**os.environ, 'RELEASE_PACKAGES': json.dumps(packages)}

            def run():
                return subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', checksum_script()],
                                      cwd=root, env=env, capture_output=True, text=True)

            missing = run()
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn('Missing or empty release package', missing.stdout)
            for package in packages:
                (root / package).write_bytes(b'fixture package')
            complete = run()
            self.assertEqual(complete.returncode, 0, complete.stdout + complete.stderr)
            manifest = (root / 'SHA256SUMS.txt').read_text().splitlines()
            self.assertEqual({line.split('  ', 1)[1] for line in manifest}, set(packages))
            (root / packages[-1]).write_bytes(b'')
            self.assertNotEqual(run().returncode, 0)
            (root / packages[-1]).unlink()
            self.assertNotEqual(run().returncode, 0)


if __name__ == '__main__':
    unittest.main()
