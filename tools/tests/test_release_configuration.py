"""Run with python3 -m unittest discover -s tools/tests.

Checks the release matrix contract and executes the workflow's real checksum step
against complete, missing and empty download fixtures.
"""
import json
import os
import platform
import shutil
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from release_metadata import package_names, package_version


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

    def test_full_validation_uses_the_release_matrix_without_publishing(self):
        workflow = (ROOT / '.github/workflows/full_build.yml').read_text()
        self.assertIn('workflow_dispatch:', workflow)
        self.assertIn('workflow_call:', workflow)
        self.assertIn('uses: ./.github/workflows/full_build.yml', (ROOT / '.github/workflows/nightly.yml').read_text())
        self.assertIn('uses: ./.github/workflows/build_and_test.yml', workflow)
        self.assertNotIn('contents: write', workflow)
        self.assertNotIn('action-gh-release', workflow)
        self.assertIn('build_matrix: ${{ needs.prepare.outputs.matrix }}', workflow)
        self.assertIn('source_ref: ${{ needs.prepare.outputs.commit }}', workflow)
        step = workflow.split('      - name: Prepare full matrix\n', 1)[1]
        body = textwrap.dedent(step.split('        run: |\n', 1)[1].split('\n  build:', 1)[0])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'outputs'
            result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', body], cwd=ROOT,
                                    env={**os.environ, 'GITHUB_OUTPUT': str(output),
                                         'GITHUB_RUN_ID': '12345', 'GITHUB_RUN_ATTEMPT': '2'},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            outputs = dict(line.split('=', 1) for line in output.read_text().splitlines())
            self.assertEqual(json.loads(outputs['matrix'])['include'], self.rows)
            numeric = (ROOT / 'VERSION').read_text().strip()
            self.assertEqual(outputs['package_version'], f'{numeric}-verify.12345.2')
            self.assertRegex(outputs['commit'], r'^[0-9a-f]{40}$')

    @unittest.skipUnless(platform.system() == 'Linux' and shutil.which('cmake') and shutil.which('c++'),
                         'The isolated SIMD configure probe requires Linux CMake/C++ tools')
    def test_simd_probe_sets_its_own_dialect_without_changing_the_project(self):
        with tempfile.TemporaryDirectory(prefix='openq-simd-configure-') as directory:
            source = Path(directory)
            header = source / 'fake/hwy/detect_targets.h'
            header.parent.mkdir(parents=True)
            header.write_text('#if __cplusplus < 201703L\n#error Probe needs C++17\n#endif\n'
                              '#define HWY_SSE2 4\n#define HWY_NEON_WITHOUT_AES 8\n'
                              '#define HWY_STATIC_TARGET HWY_BASELINE_TARGETS\n')
            target = 'NEON' if platform.machine().lower() in ('arm64', 'aarch64') else 'SSE2'
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.25)\nproject(Probe LANGUAGES CXX)\n'
                'set(CMAKE_CXX_STANDARD 98)\n'
                f'set(OPENQ_SIMD_TARGET {target})\n'
                f'include("{(ROOT / "tools/cmake/OpenQSimd.cmake").as_posix()}")\n'
                'openq_verify_simd("${CMAKE_CURRENT_SOURCE_DIR}/fake")\n'
                'if(NOT CMAKE_CXX_STANDARD EQUAL 98)\nmessage(FATAL_ERROR "Probe changed caller dialect")\nendif()\n')
            result = subprocess.run(['cmake', '-S', str(source), '-B', str(source / 'build')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('OPENQ_SIMD_TARGET_VERIFIED - Success', result.stdout)

    @unittest.skipUnless(platform.system() == 'Linux' and shutil.which('cmake'),
                         'The isolated MSVC platform configure check requires Linux CMake')
    def test_windows_validator_flags_preserve_msvc_platform_defaults(self):
        workflow = (ROOT / '.github/workflows/build_and_test.yml').read_text()
        line = next(line.strip() for line in workflow.splitlines()
                    if 'flags+=(' in line and '-DCMAKE_CXX_COMPILER=cl' in line)
        options = shlex.split(line.removeprefix('flags+=(').removesuffix(')'))
        flag_options = [option for option in options if option.startswith('-DCMAKE_CXX_FLAGS')]
        with tempfile.TemporaryDirectory(prefix='openq-msvc-flags-') as directory:
            source = Path(directory)
            # Exercise CMake's real Windows/MSVC defaults without executing a Windows compiler.
            (source / 'toolchain.cmake').write_text(
                'set(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR ARM64)\n'
                f'set(CMAKE_CXX_COMPILER "{sys.executable}")\n'
                'set(CMAKE_CXX_COMPILER_ID MSVC)\nset(CMAKE_CXX_COMPILER_VERSION 19.51)\n'
                'set(CMAKE_CXX_COMPILER_ID_RUN TRUE)\nset(CMAKE_CXX_COMPILER_FORCED TRUE)\n'
                'set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID ARM64)\n')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.25)\nproject(Flags LANGUAGES CXX)\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/flags.txt" "${CMAKE_CXX_FLAGS}")\n')
            build = source / 'build'
            result = subprocess.run(['cmake', '-S', str(source), '-B', str(build),
                                     f'-DCMAKE_TOOLCHAIN_FILE={source / "toolchain.cmake"}', *flag_options],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            flags = (build / 'flags.txt').read_text()
            self.assertIn('-FIchrono', flags)
            self.assertIn('/EHsc', flags)

    def test_tag_versions_keep_preview_identity_and_numeric_version_validation(self):
        for tag in ['v0.1.0', 'v0.1.0-beta.1', 'v0.1.0-beta.2', 'v0.1.0-rc.1']:
            with self.subTest(tag=tag):
                self.assertEqual(package_version(tag, '0.1.0'), tag[1:])
        for tag in ['v0.2.0', '0.1.0', 'v0.1.0-', 'v0.1.0;echo bad']:
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                package_version(tag, '0.1.0')

    def test_preparation_outputs_classification_and_full_versioned_packages(self):
        with tempfile.TemporaryDirectory() as directory:
            version_file = Path(directory) / 'VERSION'
            version_file.write_text('0.1.0\n')
            for tag, preview in [('v0.1.0', 'false'), ('v0.1.0-beta.2', 'true')]:
                result = subprocess.run([sys.executable, str(ROOT / 'tools/release_metadata.py'),
                                         '--tag', tag, '--commit', 'a' * 40, '--version-file', str(version_file)],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs = dict(line.split('=', 1) for line in result.stdout.splitlines())
                self.assertEqual(outputs['prerelease'], preview)
                self.assertEqual(outputs['version'], '0.1.0')
                self.assertEqual(outputs['package_version'], tag[1:])
                self.assertEqual(json.loads(outputs['packages']), package_names({'include': self.rows}, tag[1:]))

    def test_build_filenames_use_release_suffix_or_numeric_development_default(self):
        workflow = (ROOT / '.github/workflows/build_and_test.yml').read_text()
        command = next(line.strip() for line in workflow.splitlines() if 'echo "ARTIFACT_NAME=' in line)
        command = command.replace('${{ env.PRODUCT_NAME }}', 'Open-Q').replace('${{ matrix.name }}', 'Windows-x86-64')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'environment'
            for version in ['', '0.1.0-beta.2']:
                output.write_text('')
                subprocess.run(['bash', '-e', '-c', command], check=True,
                               env={**os.environ, 'PACKAGE_VERSION': version, 'VERSION': '0.1.0', 'GITHUB_ENV': str(output)})
                self.assertEqual(output.read_text().strip(), f'ARTIFACT_NAME=Open-Q-{version or "0.1.0"}-Windows-x86-64')

    def test_checksums_require_every_variant_and_cover_all_packages(self):
        packages = package_names({'include': self.rows}, '0.1.0-beta.2')
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
            self.assertEqual({path.name for path in root.iterdir()}, set(packages) | {'SHA256SUMS.txt'})
            manifest = (root / 'SHA256SUMS.txt').read_text().splitlines()
            self.assertEqual({line.split('  ', 1)[1] for line in manifest}, set(packages))
            (root / packages[-1]).write_bytes(b'')
            self.assertNotEqual(run().returncode, 0)
            (root / packages[-1]).unlink()
            self.assertNotEqual(run().returncode, 0)


if __name__ == '__main__':
    unittest.main()
