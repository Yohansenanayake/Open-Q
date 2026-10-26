# Third-party notices

Open-Q is licensed under **AGPL-3.0-only**. Copyright (c) 2026 Yohan Senanayake for Open-Q contributions. Third-party material retains its original notices and applicable licenses.

## ZLEqualizer bell coefficient design

- Author/copyright: **Copyright (C) 2026 zsliu98**.
- Project: [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer).
- Imported revision: `3ce598ee065b3d3847121e76e196feda7ed8f56d`.
- Original file: `source/dsp/filter/iir_filter/coeff/ivantsov_coeff.cpp`.
- Open-Q adaptation: `source/dsp/BellCoefficients.cpp`.
- License: GNU Affero General Public License, **version 3 only**; see [LICENSE](LICENSE).
- Research: Yuriy Ivantsov, [On the Ideal Bilinear and Biquadratic Digital Filter](https://ivantsovy.com/research/paper1.pdf).

The adaptation extracts the bell calculation and its frequency-wrapping/Phi helpers, retains ZLE's sigma and spline constants, and changes the surrounding API to named normalized biquad coefficients. Yohan Senanayake added input limits, invalid-input handling, coefficient validation, and a zero-gain identity path on 26 October 2026. Other ZLE responses, dynamic-EQ machinery, logos, and GUI assets were not imported. The smoothing and channel-state implementation in Open-Q is separate from ZLE's processing classes.

## Pamplejuce template

The original build system, plugin skeleton, and supporting template files are derived from [Pamplejuce](https://github.com/sudara/pamplejuce), copyright (c) 2022 Sudara Williams. Its complete MIT notice is retained in [licenses/Pamplejuce-MIT.txt](licenses/Pamplejuce-MIT.txt).

## Framework and dependencies

[JUCE](https://github.com/juce-framework/JUCE), melatonin_inspector, clap-juce-extensions, Catch2, and their bundled dependencies retain their own license files and copyright notices in the corresponding submodules or fetched source directories. Open-Q's project license does not replace those notices.
