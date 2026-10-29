# Shelf and cut response plots

[Method and regeneration instructions](../README.md#shelf-and-cut-exports).

48 kHz · three centres at 0.05, 0.5 and 0.8 × Nyquist · shelves Q = 1 and ±12 dB · cuts default Q = 1/√2. Each diagram shows independent filters, not their sum. Light theme, shared magnitude diagram and stacked errors. The website viewer uses these SVG/CSV exports; PNG previews remain available here.

For low-pass filters, the exact JUCE/BLT zero at Nyquist is stored as `-inf` in the CSV and omitted from the error line. Cut plots clip magnitude below −120 dB and errors outside ±60 dB; CSV retains the full data.

| Filter | Slope | Relative to Nyquist | Hz | Data |
| --- | --- | --- | --- | --- |
| Low shelf | — | [SVG](low-shelf-comparison-relative.svg) · [PNG](low-shelf-comparison-relative.png) | [SVG](low-shelf-comparison-hz.svg) · [PNG](low-shelf-comparison-hz.png) | [CSV](low-shelf-curves.csv) |
| High shelf | — | [SVG](high-shelf-comparison-relative.svg) · [PNG](high-shelf-comparison-relative.png) | [SVG](high-shelf-comparison-hz.svg) · [PNG](high-shelf-comparison-hz.png) | [CSV](high-shelf-curves.csv) |
| Low cut / high-pass | 6 dB/oct | [SVG](low-cut-6db-comparison-relative.svg) · [PNG](low-cut-6db-comparison-relative.png) | [SVG](low-cut-6db-comparison-hz.svg) · [PNG](low-cut-6db-comparison-hz.png) | [CSV](low-cut-6db-curves.csv) |
| Low cut / high-pass | 12 dB/oct | [SVG](low-cut-12db-comparison-relative.svg) · [PNG](low-cut-12db-comparison-relative.png) | [SVG](low-cut-12db-comparison-hz.svg) · [PNG](low-cut-12db-comparison-hz.png) | [CSV](low-cut-12db-curves.csv) |
| Low cut / high-pass | 18 dB/oct | [SVG](low-cut-18db-comparison-relative.svg) · [PNG](low-cut-18db-comparison-relative.png) | [SVG](low-cut-18db-comparison-hz.svg) · [PNG](low-cut-18db-comparison-hz.png) | [CSV](low-cut-18db-curves.csv) |
| Low cut / high-pass | 24 dB/oct | [SVG](low-cut-24db-comparison-relative.svg) · [PNG](low-cut-24db-comparison-relative.png) | [SVG](low-cut-24db-comparison-hz.svg) · [PNG](low-cut-24db-comparison-hz.png) | [CSV](low-cut-24db-curves.csv) |
| Low cut / high-pass | 36 dB/oct | [SVG](low-cut-36db-comparison-relative.svg) · [PNG](low-cut-36db-comparison-relative.png) | [SVG](low-cut-36db-comparison-hz.svg) · [PNG](low-cut-36db-comparison-hz.png) | [CSV](low-cut-36db-curves.csv) |
| Low cut / high-pass | 48 dB/oct | [SVG](low-cut-48db-comparison-relative.svg) · [PNG](low-cut-48db-comparison-relative.png) | [SVG](low-cut-48db-comparison-hz.svg) · [PNG](low-cut-48db-comparison-hz.png) | [CSV](low-cut-48db-curves.csv) |
| Low cut / high-pass | 72 dB/oct | [SVG](low-cut-72db-comparison-relative.svg) · [PNG](low-cut-72db-comparison-relative.png) | [SVG](low-cut-72db-comparison-hz.svg) · [PNG](low-cut-72db-comparison-hz.png) | [CSV](low-cut-72db-curves.csv) |
| Low cut / high-pass | 96 dB/oct | [SVG](low-cut-96db-comparison-relative.svg) · [PNG](low-cut-96db-comparison-relative.png) | [SVG](low-cut-96db-comparison-hz.svg) · [PNG](low-cut-96db-comparison-hz.png) | [CSV](low-cut-96db-curves.csv) |
| High cut / low-pass | 6 dB/oct | [SVG](high-cut-6db-comparison-relative.svg) · [PNG](high-cut-6db-comparison-relative.png) | [SVG](high-cut-6db-comparison-hz.svg) · [PNG](high-cut-6db-comparison-hz.png) | [CSV](high-cut-6db-curves.csv) |
| High cut / low-pass | 12 dB/oct | [SVG](high-cut-12db-comparison-relative.svg) · [PNG](high-cut-12db-comparison-relative.png) | [SVG](high-cut-12db-comparison-hz.svg) · [PNG](high-cut-12db-comparison-hz.png) | [CSV](high-cut-12db-curves.csv) |
| High cut / low-pass | 18 dB/oct | [SVG](high-cut-18db-comparison-relative.svg) · [PNG](high-cut-18db-comparison-relative.png) | [SVG](high-cut-18db-comparison-hz.svg) · [PNG](high-cut-18db-comparison-hz.png) | [CSV](high-cut-18db-curves.csv) |
| High cut / low-pass | 24 dB/oct | [SVG](high-cut-24db-comparison-relative.svg) · [PNG](high-cut-24db-comparison-relative.png) | [SVG](high-cut-24db-comparison-hz.svg) · [PNG](high-cut-24db-comparison-hz.png) | [CSV](high-cut-24db-curves.csv) |
| High cut / low-pass | 36 dB/oct | [SVG](high-cut-36db-comparison-relative.svg) · [PNG](high-cut-36db-comparison-relative.png) | [SVG](high-cut-36db-comparison-hz.svg) · [PNG](high-cut-36db-comparison-hz.png) | [CSV](high-cut-36db-curves.csv) |
| High cut / low-pass | 48 dB/oct | [SVG](high-cut-48db-comparison-relative.svg) · [PNG](high-cut-48db-comparison-relative.png) | [SVG](high-cut-48db-comparison-hz.svg) · [PNG](high-cut-48db-comparison-hz.png) | [CSV](high-cut-48db-curves.csv) |
| High cut / low-pass | 72 dB/oct | [SVG](high-cut-72db-comparison-relative.svg) · [PNG](high-cut-72db-comparison-relative.png) | [SVG](high-cut-72db-comparison-hz.svg) · [PNG](high-cut-72db-comparison-hz.png) | [CSV](high-cut-72db-curves.csv) |
| High cut / low-pass | 96 dB/oct | [SVG](high-cut-96db-comparison-relative.svg) · [PNG](high-cut-96db-comparison-relative.png) | [SVG](high-cut-96db-comparison-hz.svg) · [PNG](high-cut-96db-comparison-hz.png) | [CSV](high-cut-96db-curves.csv) |
