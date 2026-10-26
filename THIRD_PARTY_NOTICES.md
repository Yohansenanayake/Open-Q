# Third-party notices

Open-Q is licensed under **AGPL-3.0-only**. Copyright (c) 2026 Yohan Senanayake for Open-Q contributions. Third-party material retains its original notices and applicable licenses.

## ZLEqualizer bell, shelf and cut coefficient designs

- Author/copyright: **Copyright (C) 2026 zsliu98**.
- Project: [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer).
- Imported revision: `3ce598ee065b3d3847121e76e196feda7ed8f56d`.
- Original files: `source/dsp/filter/iir_filter/coeff/ivantsov_coeff.cpp` and the cut assembly in `source/dsp/filter/filter_design/filter_design.hpp`.
- Open-Q adaptation: `source/dsp/FilterCoefficients.cpp`.
- License: GNU Affero General Public License, **version 3 only**; see [LICENSE](LICENSE).
- Research: Yuriy Ivantsov, [On the Ideal Bilinear and Biquadratic Digital Filter](https://ivantsovy.com/research/paper1.pdf).

The adaptation extracts the bell and second-order low/high shelf calculations and their shared frequency-wrapping/Phi helpers, retains ZLE's sigma and spline constants, and changes the surrounding API to named normalized biquad coefficients. Yohan Senanayake added input limits, invalid-input handling, coefficient validation, and a zero-gain identity path on 26 October 2026. On 27 October 2026, the second-order `get2LowShelf`/`get2HighShelf` formulas from the same pinned revision were added with response-type dispatch; the bell calculation is unchanged. Also on 27 October 2026, first/second-order low/high-pass sections and the even-order pass-filter assembly were adapted from the same revision. Open-Q adds fixed-capacity named section storage, section validation, and a separately validated third-order cascade using the established odd-order Butterworth section arrangement with internal Q=sqrt(2)*user Q. The third-order branch is not present in ZLE. Existing bell/shelf formulas and their revisions are preserved. Other ZLE responses, dynamic-EQ machinery, logos, and GUI assets were not imported. The smoothing and channel-state implementation in Open-Q is separate from ZLE's processing classes.

## ZLEqualizer triple-buffer handoff

- Author/copyright: **Copyright (C) 2026 zsliu98**.
- Source: `source/panel/helper/tri_buffer.hpp` at ZLEqualizer commit `3ce598ee065b3d3847121e76e196feda7ed8f56d`.
- Open-Q adaptation: `source/utility/TripleBuffer.h`, under **AGPL-3.0-only**.
- Modifications by Yohan Senanayake, 26 October 2026: renamed the namespace/type, default-initialized payload slots, removed raw-array access, and applied Open-Q formatting. The single-producer/single-consumer exchange and its memory ordering are retained.

The response worker and frame-driven UI organization follow the studied ZLE pattern. Open-Q calculates its target response from parameter values using its DSP coefficient-design function, rather than importing ZLE's ideal-prototype response models or graph components. The graph worker reads parameter atomics independently of audio processing.

Open-Q's fixed-slot bank and shared selected-band controls are also informed by the same pinned ZLE revision (`zlp/controller.*`, `zlp/zlp_definitions.hpp`, and `panel/control_panel/left_control_panel.cpp`). The bank, parameter bindings, three-state band lifecycle (Off/Bypass/On), wet/dry fades, and combined digital response are implemented separately in Open-Q; no additional ZLE processing or UI source was imported for this feature.

## ZLEqualizer node interaction reference

Open-Q's node interaction follows the source study of ZLEqualizer commit `3ce598ee065b3d3847121e76e196feda7ed8f56d`, copyright (C) 2026 zsliu98, under AGPL-3.0-only. Relevant sources are `source/gui/dragger/dragger_component.cpp`, `dragger_attachment.hpp`, and `source/panel/curve_panel/response_panel/dragger_panel/`. Open-Q implements relative dragging, shelf half-gain node placement and inverse gain mapping, display-to-host parameter conversion, three-state node visibility, and host gesture handling in `source/ui/ResponseGraph.cpp`. It uses its own bell-only creation defaults, digital response paths, parameter-polling node positions, and fixed shortcuts. The selection-driven band card also follows the studied `panel/control_panel/` and floating-popup pattern, with Open-Q’s existing parameter attachments and status controls. The Spectrum icon imports are recorded separately below; no excluded logos are imported.

## ZL Spectrum Equalizer research reference

[ZL Spectrum Equalizer](https://github.com/ZL-Audio/ZLSpectrumEqualizer), copyright (C) 2026 zsliu98, is Open-Q's second implementation reference. Its UI and architecture were studied at commit `10388f0cb594c6a322f8652cd718aed28c2e2c39`. Source headers specify AGPL version 3, and its README excludes the ZL Audio and Spectrum Equalizer logos. The initial research imported no code or assets. On 27 October 2026, Open-Q copied the following files unchanged from that revision, retaining existing source notices:

- `assets/icons/bypass.svg` → `assets/icons/bypass.svg`.
- `assets/icons/close.svg` → `assets/icons/close.svg`.
- `assets/icons/trash.svg` → `assets/icons/trash.svg`.
- `assets/icons/main.py` → `tools/format_svg_icons.py`.

These files are used under AGPL-3.0-only; see [LICENSE](LICENSE). Button colors and interaction states are applied in C++ rather than altering the SVGs. The formatting helper is stored outside `assets/` so it is not embedded in plugin binaries. It adds a transparent bounds rectangle and normalizes `black` to `#000000` for multiline SVGs in the current directory. Neither `logo.svg` nor `zlaudio.svg` was copied, and no new dependencies were added.

## Pamplejuce template

The original build system, plugin skeleton, and supporting template files are derived from [Pamplejuce](https://github.com/sudara/pamplejuce), copyright (c) 2022 Sudara Williams. Its complete MIT notice is retained in [licenses/Pamplejuce-MIT.txt](licenses/Pamplejuce-MIT.txt).

## Framework and dependencies

[JUCE](https://github.com/juce-framework/JUCE), melatonin_inspector, clap-juce-extensions, Catch2, and their bundled dependencies retain their own license files and copyright notices in the corresponding submodules or fetched source directories. Open-Q's project license does not replace those notices.
