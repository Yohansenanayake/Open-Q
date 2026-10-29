"""Exercise the compatibility commands published in the installation guide."""
from html import unescape
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LINUX_FEATURES = 'avx avx2 bmi1 bmi2 f16c fma abm movbe xsave aes pclmulqdq cx16 lahf_lm popcnt pni ssse3 sse4_1 sse4_2'.split()
WINDOWS_FEATURES = 'AVX AVX2 BMI1 BMI2 F16C FMA LZCNT MOVBE XSAVE OSXSAVE AES PCLMULDQ CX16 LAHF-SAHF POPCNT SSE3 SSSE3 SSE4.1 SSE4.2'.split()
POWERSHELL = shutil.which('pwsh') or shutil.which('powershell')


def command(element):
    html = (ROOT / 'website/install.html').read_text()
    return unescape(re.search(r'<code id="' + element + r'">(.*?)</code>', html, re.S).group(1))


class InstallationCheckTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('awk'), 'The Linux guide uses awk')
    def test_linux_choice_requires_all_features_on_every_processor(self):
        args = shlex.split(command('linux-avx2-check'))
        self.assertEqual(args[-1], '/proc/cpuinfo')
        complete = 'flags\t: ' + ' '.join(LINUX_FEATURES) + '\n'
        cases = [(complete, True), (complete + complete, True), ('', False),
                 ('Features: asimd aes\n', False)]
        for feature in LINUX_FEATURES:
            incomplete = 'flags : ' + ' '.join(f for f in LINUX_FEATURES if f != feature) + '\n'
            cases.append((incomplete, False))
            cases.append((complete + incomplete, False))
        cases.append((complete.replace(' avx2 ', ' avx512f '), False))
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / 'cpuinfo'
            for report, avx2 in cases:
                with self.subTest(report=report):
                    fixture.write_text(report)
                    result = subprocess.run([*args[:-1], str(fixture)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(), 'Choose Linux x86-64' + (' AVX2' if avx2 else ''))

    @unittest.skipUnless(POWERSHELL, 'PowerShell is needed to exercise the Windows guide')
    def test_windows_choice_handles_missing_features_and_coreinfo_errors(self):
        script = command('windows-avx2-check')
        invocation = '$report = & .\\Coreinfo64.exe -f 2>&1 | Out-String'
        self.assertIn(invocation, script)
        # Supply Coreinfo output without running the downloaded executable.
        script = script.replace(invocation, '$report = $env:OPENQ_COREINFO_REPORT\n'
                                            '$LASTEXITCODE = [int]$env:OPENQ_COREINFO_EXIT')
        complete = '\n'.join(f'{feature}\t*\tSupported' for feature in WINDOWS_FEATURES)
        cases = [(complete, 0, True), (complete, 1, False), ('', 0, False)]
        for feature in ('AVX2', 'AES', 'PCLMULDQ', 'OSXSAVE', 'FMA', 'SSE4.1'):
            cases.append((complete.replace(f'{feature}\t*', f'{feature}\t-'), 0, False))
        cases.append((complete.replace('SSE4.1', 'SSE4x1'), 0, False))
        cases.append((complete.replace('AVX2', 'AVX512F'), 0, False))
        for report, exit_code, avx2 in cases:
            with self.subTest(report=report, exit_code=exit_code):
                result = subprocess.run([POWERSHELL, '-NoProfile', '-NonInteractive', '-Command', script + '\nexit 0'],
                                        env={**os.environ, 'OPENQ_COREINFO_REPORT': report,
                                             'OPENQ_COREINFO_EXIT': str(exit_code)},
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), 'Choose Windows x86-64' + (' AVX2' if avx2 else ''))


if __name__ == '__main__':
    unittest.main()
