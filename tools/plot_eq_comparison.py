#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Render EqComparison CSV as matching Nyquist-relative and Hz SVG/PNG plots."""
import argparse
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CURVES = (
    ('analogue_db', 'Analogue reference', '#24262b', '-'),
    ('openq_db', 'Open-Q · Ivantsov', '#0066b3', '-'),
    ('juce_db', 'JUCE', '#c24712', '-'),
    ('blt_db', 'Prewarped BLT', '#147d45', '--'),
)
METADATA = ('sample_rate_hz', 'centre_nyquist', 'centre_hz', 'gain_db', 'q', 'filter_type', 'slope_db_oct')
FAMILIES = {0: ('bell', 'Bell'), 1: ('low-shelf', 'Low shelf'), 2: ('high-shelf', 'High shelf'),
            3: ('low-cut', 'Low cut / high-pass'), 4: ('high-cut', 'High cut / low-pass')}


def load_cases(path):
    cases = {}
    with path.open(newline='') as source:
        for row in csv.DictReader(source):
            values = {key: float(value) for key, value in row.items()}
            for key, value in values.items():
                exact_zero = (values.get('filter_type') == 4 and values.get('frequency_nyquist') == 1
                              and key in ('juce_db', 'blt_db') and value == -math.inf)
                if not math.isfinite(value) and not exact_zero:
                    raise ValueError('Only exact low-pass zeros at Nyquist may be non-finite')
            relative = values['frequency_nyquist']
            rate = values['sample_rate_hz']
            if rate <= 0 or not 0 < relative <= 1 or not math.isclose(
                    values['frequency_hz'], relative * rate / 2, rel_tol=1e-12):
                raise ValueError('Invalid frequency coordinates')
            case = cases.setdefault(row['case_id'], [])
            if case and (any(values.get(key) != case[0].get(key) for key in METADATA)
                         or relative <= case[-1]['frequency_nyquist']):
                raise ValueError('Inconsistent metadata or unordered frequency grid')
            case.append(values)
    if not cases:
        raise ValueError('Comparison CSV is empty')
    return cases


def axis_values(rows, style):
    if style not in ('relative', 'hz'):
        raise ValueError('Axis must be relative or hz')
    return [row['frequency_nyquist' if style == 'relative' else 'frequency_hz'] for row in rows]


def axis_ticks(style, sample_rate):
    relative = [0.001, 0.01, 0.1, 0.5, 1]
    if style == 'relative':
        ticks = relative
        labels = [f'{value:g}' for value in ticks]
    elif style == 'hz':
        ticks = [value * sample_rate / 2 for value in relative]
        labels = [f'{value / 1000:g}k' if value >= 1000 else f'{value:g}' for value in ticks]
    else:
        raise ValueError('Axis must be relative or hz')
    labels[-1] += '\n(Nyquist)'
    return ticks, labels


def render(cases, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter

    output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({
        'font.family': 'DejaVu Sans', 'font.size': 15,
        'figure.facecolor': '#ffffff', 'axes.facecolor': '#ffffff',
        'text.color': '#25262c', 'axes.labelcolor': '#34343d',
        'xtick.color': '#50515c', 'ytick.color': '#50515c',
        'axes.edgecolor': '#a7a8b0', 'svg.fonttype': 'none',
        'svg.hashsalt': 'openq-eq-comparison',
    })
    settings = next(iter(cases.values()))[0]
    family = int(settings.get('filter_type', 0))
    stem, title = FAMILIES[family]
    cut = family in (3, 4)
    if cut:
        slope = int(settings['slope_db_oct'])
        stem += f'-{slope}db'
    for rows in cases.values():
        if any(rows[0].get(key) != settings.get(key) for key in ('sample_rate_hz', 'q', 'filter_type', 'slope_db_oct')):
            raise ValueError('A figure must contain a single filter family, slope, Q and sample rate')
        for row in rows:
            if not (row['juce_db'] == row['blt_db'] or abs(row['juce_db'] - row['blt_db']) < 1e-6):
                raise ValueError('Cannot share the error trace when JUCE and BLT disagree')
    centres = sorted({rows[0]['centre_nyquist'] for rows in cases.values()})
    for style in ('relative', 'hz'):
        fig, axes = plt.subplots(1 + len(centres), 1, figsize=(12, 11), sharex=True,
                                 gridspec_kw={'height_ratios': [3] + [1] * len(centres)},
                                 layout='constrained')
        for index, rows in enumerate(cases.values()):
            x = axis_values(rows, style)
            for key, label, colour, line_style in CURVES:
                axes[0].plot(x, [row[key] for row in rows],
                             label=label if index == 0 else None, color=colour,
                             linestyle=line_style, linewidth=2.2)
            error_axis = axes[1 + centres.index(rows[0]['centre_nyquist'])]
            gain_style = '-' if cut or rows[0]['gain_db'] > 0 else '--'
            # JUCE/BLT agreement is checked by the exporter; share their error trace.
            for key, colour in [('openq_db', CURVES[1][2]), ('juce_db', CURVES[2][2])]:
                # Leave exact -infinity endpoint errors as gaps, never a fabricated dB floor.
                error = [row[key] - row['analogue_db'] if math.isfinite(row[key]) else math.nan for row in rows]
                error_axis.plot(x, error, color=colour, linestyle=gain_style, linewidth=2)
        for ax in axes:
            ax.set_xscale('log')
            ax.set_xlim(x[0], x[-1])
            ax.grid(True, which='major', color='#e1e2e8', linewidth=0.8)
            ax.xaxis.set_minor_formatter(NullFormatter())
            ax.axhline(0, color='#a7a8b0', linewidth=0.8, zorder=0)
        axes[0].set_ylim(-120 if cut else -16, 12 if cut else 16)
        axes[0].set_yticks([-120, -96, -72, -48, -24, 0] if cut else [-12, -6, 0, 6, 12])
        axes[0].set_ylabel('Magnitude (dB)')
        if family == 0:
            axes[0].legend(loc='upper left', ncols=2, frameon=False, fontsize=14)
        else:
            axes[0].legend(loc='lower left', bbox_to_anchor=(0, 1.02), ncols=4, frameon=False, fontsize=14)
        for ax, centre in zip(axes[1:], centres):
            error_limit = 60 if cut else 12
            ax.set_ylim(-error_limit, error_limit)
            ax.set_yticks([-error_limit, 0, error_limit])
            ax.set_ylabel('Error (dB)')
            ax.text(0.015, 0.85, f'f₀ = {centre:g} × Nyquist', transform=ax.transAxes,
                    fontsize=14, va='top', bbox={'facecolor': 'white', 'edgecolor': 'none', 'pad': 2})
        from matplotlib.lines import Line2D
        error_legend = [Line2D([], [], color=CURVES[1][2], label='Open-Q'),
                        Line2D([], [], color=CURVES[2][2], label='JUCE / BLT'),
                        Line2D([], [], color='#42444a', label='Boost', linestyle='-'),
                        Line2D([], [], color='#42444a', label='Cut', linestyle='--')]
        axes[1].legend(handles=error_legend[:2] if cut else error_legend, loc='lower left',
                       ncols=2 if cut else 4, frameon=False, fontsize=14)
        ticks, labels = axis_ticks(style, settings['sample_rate_hz'])
        axes[-1].xaxis.set_major_locator(FixedLocator(ticks))
        axes[-1].xaxis.set_major_formatter(FixedFormatter(labels))
        axes[-1].set_xlabel('Frequency / Nyquist (log scale)' if style == 'relative'
                            else 'Frequency (Hz, log scale)')
        configuration = f'{slope} dB/oct · Butterworth prototype' if cut else '±12 dB'
        fig.suptitle(f"{title} response comparison · {configuration}\n"
                     f"Q = {settings['q']:.6g} · fₛ = {settings['sample_rate_hz'] / 1000:g} kHz", fontsize=18)
        if cut:
            endpoint_note = ('Exact JUCE/BLT −∞ at Nyquist is omitted from error traces.' if family == 4
                             else 'Each response is an independent band, not their combined response.')
            fig.supxlabel('Visible magnitude ≥ −120 dB; error panels ±60 dB (curves clipped).\n'
                          + endpoint_note, fontsize=14)
        for extension in ('svg', 'png'):
            fig.savefig(output / f'{stem}-comparison-{style}.{extension}', dpi=160,
                        metadata={'Date': None} if extension == 'svg' else {})
        plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'website/assets/eq-comparison')
    args = parser.parse_args()
    render(load_cases(args.csv), args.output)


if __name__ == '__main__':
    main()
