# EQ response comparisons

An offline C++ exporter evaluates Open-Q's **production** `makeBandCoefficients` function and the checked-out JUCE `IIR::ArrayCoefficients<double>` constructors. A Python script renders the same CSV in two axis styles. This tool is separate from the plugin build and does not change audio processing.

The checked-in bell examples use 48 kHz, Q = 1, ±12 dB and centres at 0.05 / 0.5 / 0.8 × Nyquist (1.2 / 12 / 19.2 kHz). All centres are inside Open-Q's supported range; the exporter rejects cases that would be clamped. Frequencies span 0.001–1 × Nyquist on a logarithmic grid, including each exact centre and Nyquist. The plots show the complete positive-frequency response up to 24 kHz, including the region above 20 kHz.

## Reproduce

Initialize submodules first. These commands use Ninja and Unix executable paths:

```bash
cmake -S tools/eq-comparison -B Builds/eq-comparison -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds/eq-comparison --target EqComparison --parallel 2
./Builds/eq-comparison/EqComparison website/assets/eq-comparison/bell-curves.csv

python3 -m venv Builds/plot-venv
Builds/plot-venv/bin/python -m pip install -r tools/eq-comparison/requirements.txt
Builds/plot-venv/bin/python tools/plot_eq_comparison.py website/assets/eq-comparison/bell-curves.csv
python3 -m unittest discover -s tools/tests
```

On Windows, use `EqComparison.exe` and the virtual environment's `Scripts/python.exe`. The plotting environment needs Python 3.11 or newer. Matplotlib is only an offline development dependency; it is not shipped in the plugin or needed by Pages deployments.

Outputs are committed under [`website/assets/eq-comparison`](../../website/assets/eq-comparison):

- `bell-curves.csv`: settings, both frequency coordinates and all four responses for six cases. IDs 0–2 are boosts in ascending centre-frequency order; 3–5 are cuts.
- `bell-comparison-relative.svg` / `.png`: frequency divided by Nyquist.
- `bell-comparison-hz.svg` / `.png`: the same response with the frequency axis in Hz.

One figure overlays all six independent bell cases in its main magnitude diagram, then stacks three error panels by centre frequency. The curves are not cascaded or summed. Both axis styles use a light theme, matching tick positions and identical dB limits. Click a website plot to open the selected style at full size; smaller screens scroll the figure horizontally to preserve legible labels. The website defaults to Bell with frequency in Hz. Two centred axis buttons and a second row of filter-type buttons switch the static SVG, full-size link, settings caption and CSV link. Cut selections expose a shared slope selector, initially 12 dB/octave; switching filters retains the selected axis and slope. Without JavaScript the default Hz bell plot remains visible. Regenerate and review the committed data and images when coefficient code, JUCE revision or example settings change. Pages copies these assets; it does not build C++ or install plotting dependencies.

## Method and checks

Let `G = 10^(gain_dB/20)`, `A = sqrt(G)` and `s = j*f/f0`. The analogue bell is:

```
H(s) = (s² + A*s/Q + 1) / (s² + s/(A*Q) + 1)
```

The explicit BLT baseline substitutes `s = cot(pi*f0/fs)*(1-z^-1)/(1+z^-1)` into this prototype, preserving the centre frequency. This is equivalent to JUCE's Q-based RBJ bell. Their curves intentionally overlap; their maximum sampled disagreement in the initial export was below `1.4e-13 dB`. The exporter checks agreement within `1e-8 dB` across every plotted sample, finite coefficients/responses, strict denominator stability, centre gain, JUCE's unity DC/Nyquist endpoints, Open-Q zero-gain identity and Open-Q paired boost/cut cancellation. Checks remain enabled in Release builds.

Each stacked error panel subtracts the matching analogue magnitude from the digital magnitudes in dB, with identical ±12 dB limits across panels. Solid error traces are boosts and dashed traces are cuts. Open-Q is blue; JUCE/BLT share an orange error trace because their numerical agreement is checked. All four implementations remain separately drawn in the main magnitude diagram. These are steady-state transfer functions evaluated directly in double precision, without FFT analysis, oversampling, automation or fades. Magnitude similarity is one design criterion; it does not establish listening preference, phase equivalence or overall plugin quality. The website viewer includes bells, shelves and cuts; the additional exports are described below.

Sources: [RBJ Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/), the pinned JUCE implementation, and Open-Q's ZLE-derived `zle-ivantsov-v1` design. The normalized-axis presentation follows [Laurent de Soras's comparison](https://ldesoras.fr/doc/articles/decramp-comp/) and the locally inspected `pedalevite/src/test/TestEqZ` / `DesignEq2p::make_mid_peak`. No plotting code from that project was copied. The initial JUCE checkout was `620b879ef7aaba832c5a36d9040ac54ed29025c1`.


## Shelf and cut exports

[Browse the shelf/cut plots and CSV files](results/README.md). This results directory is the canonical source for shelf/cut exports. The site builder copies its SVG and CSV files into the website comparison assets; PNG previews stay in the repository. No export generation happens during Pages deployment.

| Family | Settings |
| --- | --- |
| Low shelf / high shelf | Q = 1; ±12 dB; second-order shelf midpoint convention. |
| Low cut (high-pass) / high cut (low-pass) | Default Q = 1/√2; 6, 12, 18, 24, 36, 48, 72 and 96 dB/octave. One figure per type/slope. Q is inactive for first-order cuts. |

All examples use 48 kHz and the same three Nyquist-relative centres as the bells. Each figure retains a shared magnitude diagram and three stacked error panels, with SVG and PNG exports in both frequency-axis styles. Shelf error panels distinguish boosts/cuts; cut filters have no gain control and use a single error trace per implementation. JUCE/BLT share an error trace only after their numerical agreement has been checked.

After building the exporter and installing the plotting requirements above:

```bash
./Builds/eq-comparison/EqComparison --other-filters tools/eq-comparison/results
for csv in tools/eq-comparison/results/*-curves.csv; do
    Builds/plot-venv/bin/python tools/plot_eq_comparison.py "$csv" --output tools/eq-comparison/results
done
python3 -m unittest discover -s tools/tests
```

Shelf prototypes follow the Q-based RBJ convention. With `A = 10^(gain_dB/40)`:

```
Low shelf:  A * (s² + sqrt(A)*s/Q + A) / (A*s² + sqrt(A)*s/Q + 1)
High shelf: A * (A*s² + sqrt(A)*s/Q + 1) / (s² + sqrt(A)*s/Q + A)
```

The analogue reference is evaluated directly at `s = j*f/f0`. Each first/second-order analogue section is independently transformed with the centre-frequency-prewarped BLT. JUCE uses its native shelf, first-order cut and Q-based second-order cut constructors. Higher-order JUCE/BLT cuts cascade Butterworth sections: a first-order stage where required, a Q = 1 biquad for 18 dB/octave, and even-order pole pairs `Qᵢ = 1 / (2*cos((2i+1)*pi/(2N)))`. This matches Open-Q's default-Q layout; it does not claim JUCE supplies a standalone 96 dB/octave constructor. Additional user-Q/resonance settings are not covered.

The exporter checks section counts, strict stability, centre gains, shelf DC anchors/boost-cut cancellation, finite responses and JUCE/BLT agreement within 1e-6 dB. Tests independently verify the cut reference against the Butterworth magnitude `−10*log10(1 + r^(2N))` for low-pass or `−10*log10(1 + r^(−2N))` for high-pass. These checks validate the plotted cases, not every supported parameter setting.

**Exact zeros and display limits:** JUCE/BLT low-pass filters have an exact zero at Nyquist. Their CSV value is intentionally `-inf` (−∞ dB), not an invented finite floor. This is the only non-finite value accepted by the plot loader. That endpoint is omitted from the error line. Known cut zeros are factored during evaluation to avoid cancellation in deep stopbands. Cut magnitude panels show −120..+12 dB and error panels ±60 dB; curves beyond these limits are clipped and the figure states this. CSV data retains the deeper responses. Shelf limits are ±16 dB with ±12 dB error panels. Both axis styles use identical scales and data.
