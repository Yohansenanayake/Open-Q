"""Offline plot data and axis checks; Matplotlib is only needed to regenerate artwork."""
import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from plot_eq_comparison import axis_ticks, axis_values, load_cases


class EqComparisonTests(unittest.TestCase):
    def test_axis_styles_only_rescale_frequency_and_keep_matching_grid_positions(self):
        rows = [{'frequency_nyquist': value, 'frequency_hz': value * 24000}
                for value in [0.001, 0.1, 1.0]]
        self.assertEqual(axis_values(rows, 'relative'), [0.001, 0.1, 1.0])
        self.assertEqual(axis_values(rows, 'hz'), [24, 2400, 24000])
        for rate in [44100, 48000, 96000]:
            relative, _ = axis_ticks('relative', rate)
            hz, labels = axis_ticks('hz', rate)
            self.assertEqual(hz, [value * rate / 2 for value in relative])
            self.assertTrue(labels[-1].endswith('(Nyquist)'))
        with self.assertRaises(ValueError):
            axis_values(rows, 'invalid')

    def test_exported_bells_are_finite_and_juce_matches_blt(self):
        cases = load_cases(ROOT / 'website/assets/eq-comparison/bell-curves.csv')
        self.assertEqual(len(cases), 6)
        for rows in cases.values():
            settings = rows[0]
            self.assertEqual(settings['sample_rate_hz'], 48000)
            self.assertEqual(settings['q'], 1)
            self.assertLessEqual(settings['centre_hz'], 20000)
            self.assertGreater(len(rows), 1000)
            self.assertAlmostEqual(rows[-1]['frequency_nyquist'], 1)
            centre = next(row for row in rows if row['frequency_nyquist'] == settings['centre_nyquist'])
            for key in ['analogue_db', 'openq_db', 'juce_db', 'blt_db']:
                self.assertAlmostEqual(centre[key], settings['gain_db'], places=6)
            for row in rows:
                self.assertLess(abs(row['juce_db'] - row['blt_db']), 1e-8)
        for style in ['relative', 'hz']:
            for suffix in ['svg', 'png']:
                path = ROOT / f'website/assets/eq-comparison/bell-comparison-{style}.{suffix}'
                self.assertGreater(path.stat().st_size, 0)
            svg = (ROOT / f'website/assets/eq-comparison/bell-comparison-{style}.svg').read_text()
            for centre in ['0.05', '0.5', '0.8']:
                self.assertIn(f'f₀ = {centre} × Nyquist', svg)
            self.assertIn('JUCE / BLT', svg)
            self.assertEqual(svg.count('>Error (dB)</text>'), 3)

    def test_shelves_and_all_cut_slopes_have_matching_data_and_plots(self):
        output = ROOT / 'tools/eq-comparison/results'
        slopes = [6, 12, 18, 24, 36, 48, 72, 96]
        expected = ['low-shelf', 'high-shelf'] + [f'{direction}-cut-{slope}db'
                    for direction in ['low', 'high'] for slope in slopes]
        self.assertEqual({path.name for path in output.glob('*-curves.csv')},
                         {f'{stem}-curves.csv' for stem in expected})
        for stem in expected:
            with self.subTest(stem=stem):
                cases = load_cases(output / f'{stem}-curves.csv')
                cut = '-cut-' in stem
                self.assertEqual(len(cases), 3 if cut else 6)
                for rows in cases.values():
                    settings = rows[0]
                    self.assertEqual(settings['sample_rate_hz'], 48000)
                    self.assertAlmostEqual(settings['q'], 1 / math.sqrt(2) if cut else 1)
                    self.assertEqual(settings['slope_db_oct'], int(stem.split('-')[-1][:-2]) if cut else 0)
                    self.assertEqual(rows[-1]['frequency_nyquist'], 1)
                    centre = next(row for row in rows if row['frequency_nyquist'] == settings['centre_nyquist'])
                    expected_gain = -10 * math.log10(2) if cut else settings['gain_db'] / 2
                    for key in ['analogue_db', 'juce_db', 'blt_db']:
                        self.assertAlmostEqual(centre[key], expected_gain, places=6)
                    if cut:
                        self.assertAlmostEqual(centre['openq_db'], expected_gain, places=6)
                        order = settings['slope_db_oct'] / 6
                        for row in rows:
                            ratio = row['frequency_nyquist'] / settings['centre_nyquist']
                            analytic = -10 * math.log10(1 + (1 / ratio if stem.startswith('low-cut') else ratio) ** (2 * order))
                            self.assertAlmostEqual(row['analogue_db'], analytic, places=7)
                    for row in rows:
                        if stem.startswith('high-cut') and row['frequency_nyquist'] == 1:
                            self.assertEqual(row['juce_db'], -math.inf)
                            self.assertEqual(row['blt_db'], -math.inf)
                            self.assertTrue(math.isfinite(row['analogue_db']))
                            self.assertTrue(math.isfinite(row['openq_db']))
                        else:
                            self.assertLess(abs(row['juce_db'] - row['blt_db']), 1e-6)
                for style in ['relative', 'hz']:
                    for extension in ['svg', 'png']:
                        self.assertGreater((output / f'{stem}-comparison-{style}.{extension}').stat().st_size, 0)
                    svg = (output / f'{stem}-comparison-{style}.svg').read_text()
                    self.assertEqual(svg.count('>Error (dB)</text>'), 3)

    def test_csv_rejects_bad_coordinates_nonfinite_and_empty_data(self):
        fields = ['case_id', 'sample_rate_hz', 'centre_nyquist', 'centre_hz', 'gain_db', 'q',
                  'frequency_nyquist', 'frequency_hz', 'analogue_db', 'openq_db', 'juce_db', 'blt_db']
        row = [0, 48000, 0.5, 12000, 12, 1, 0.1, 2400, 0, 0, 0, 0]
        for invalid in [[], [row[:7] + [100] + row[8:]], [row[:-1] + [math.nan]], [row[:-1] + [-math.inf]], [row, row]]:
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'test.csv'
                with path.open('w', newline='') as target:
                    writer = csv.writer(target)
                    writer.writerow(fields)
                    writer.writerows(invalid)
                with self.assertRaises(ValueError):
                    load_cases(path)


if __name__ == '__main__':
    unittest.main()
