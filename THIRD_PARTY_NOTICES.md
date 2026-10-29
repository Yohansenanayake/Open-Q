# Third-party notices

Open-Q source code is licensed under **AGPL-3.0-only**. Copyright (c) 2026 Yohan Senanayake for Open-Q contributions. The Open-Q logo in `assets/OpenQ_Logo_White.svg` is excluded from AGPL coverage; all rights reserved, as stated in [its separate notice](licenses/OpenQ-Logo.txt). Third-party material retains its original notices and applicable licenses.

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

## ZLEqualizer wheel interaction reference

Open-Q's `source/ui/GainControl.cpp` wheel handling follows `source/gui/slider/extra_slider/snapping_slider.h` at ZLEqualizer commit `3ce598ee065b3d3847121e76e196feda7ed8f56d`, copyright (C) 2026 zsliu98, under AGPL-3.0-only. It follows the timestamp de-duplication, dominant-axis/reversed-direction handling and JUCE host-gesture pattern. Open-Q uses its own dB increments and fractional smooth-scroll scaling, ignores inertial and text-editing input, and retains continuous trim parameters and the existing rendering cadence.

## ZL Spectrum Equalizer research reference

[ZL Spectrum Equalizer](https://github.com/ZL-Audio/ZLSpectrumEqualizer), copyright (C) 2026 zsliu98, is Open-Q's second implementation reference. Its UI and architecture were studied at commit `10388f0cb594c6a322f8652cd718aed28c2e2c39`. Source headers specify AGPL version 3, and its README excludes the ZL Audio and Spectrum Equalizer logos. The initial research imported no code or assets. On 27 October 2026, Open-Q copied the following files unchanged from that revision, retaining existing source notices:

- `assets/icons/bypass.svg` → `assets/icons/bypass.svg`.
- `assets/icons/close.svg` → `assets/icons/close.svg`.
- `assets/icons/trash.svg` → `assets/icons/trash.svg`.
- `assets/icons/main.py` → `tools/format_svg_icons.py`.

These files are used under AGPL-3.0-only; see [LICENSE](LICENSE). Button colors and interaction states are applied in C++ rather than altering the SVGs. The formatting helper is stored outside `assets/` so it is not embedded in plugin binaries. It adds a transparent bounds rectangle and normalizes `black` to `#000000` for multiline SVGs in the current directory. Neither `logo.svg` nor `zlaudio.svg` was copied, and no new dependencies were added.

## ZLEqualizer spectrum analyzer and FFT dependencies

- Reference: [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer) commit `3ce598ee065b3d3847121e76e196feda7ed8f56d`, copyright (C) 2026 zsliu98, AGPL-3.0-only.
- Imported unchanged on 27 October 2026: `source/dsp/analyzer/fft_analyzer/spectrum_smoother.hpp` and `spectrum_blender.hpp`, stored as `source/dsp/analyzer/reference/SpectrumSmoother.h` and `SpectrumBlender.h`. Original copyright and license headers are retained.
- Open-Q's sample transport, history, FFT wrapper, worker and drawing integration are separate implementations informed by ZLE's sender/receiver/processor and FFTPanel. They use fixed packet capture, discontinuity/session tags, averaged channel powers, a sample-rate-aware analysis-only 5 Hz DC blocker, peak-sine Hann normalization, coherent pre/post frames and explicit stale-data decay. Frequency smoothing uses the imported helper with ZLE's half-ERB default for all active resolutions. The High mode follows ZLE's resolution orders, noise-power normalization and blend ranges. These adaptations are by Yohan Senanayake, 27 October 2026.
- Imported/adapted on 27 October 2026 from the same ZLE revision: `source/dsp/analyzer/fft_analyzer/spectrum_decayer.hpp` → `source/dsp/analyzer/reference/SpectrumDecayer.h`. Original AGPLv3 copyright/license notices remain intact. The adaptation replaces the reference's vector umbrella include and aligned-vector alias with the existing pinned Highway headers and `hwy::AlignedVector`; its decay recurrence is unchanged. Open-Q configures the helper from elapsed worker time and the displayed analyzer minimum, retaining spectrum-domain histories across resizing. The extra-speed multiplier stays at the reference default; users select its five decay presets.
- [`zldsp_fft`](https://github.com/ZL-Audio/zldsp_fft), pinned to `dfc50e6c39680d9ee04fbb60fc6c935552630045`, is fetched through CPM and used unchanged. License: Apache-2.0; retained in [licenses/zldsp_fft-Apache-2.0.txt](licenses/zldsp_fft-Apache-2.0.txt).
- [`Highway`](https://github.com/ZL-Audio/highway), pinned to `2607d3b5b0113992fe84d3848859eae13b3b52c1`, is fetched through CPM and used unchanged with static dispatch. Default builds follow the compiler baseline; release variants explicitly select SSE2, AVX2 or baseline NEON via `tools/cmake/OpenQSimd.cmake`, following the pinned ZLE release approach. Highway offers Apache-2.0 or BSD-3-Clause for its main code; Open-Q uses the Apache-2.0 option. The complete upstream license text, including its separately licensed random helper (not used here), is retained in [licenses/Highway-LICENSE.txt](licenses/Highway-LICENSE.txt). Upstream source notices remain in the fetched dependency sources.

Open-Q's `source/ui/CompactComboBox.*` follows the compact styling, control caching and explicitly parented popup pattern in `source/gui/combobox/compact_combobox/` from both pinned ZLE references above. It is an independent Open-Q implementation using a JUCE ComboBox subclass and the existing JUCE parameter attachments. It locates the actual AudioProcessorEditor rather than assuming a host child index, prevents native-popup fallback, and clears popup LookAndFeel references during teardown. No additional ZLE or JUCE-fork source was copied for this control.

Open-Q's static graph artwork follows the separate buffered-component pattern in the pinned references' `source/panel/curve_panel/curve_panel.cpp` and scale-label panels. `GraphArtwork` contains Open-Q's existing drawing code and uses JUCE's standard `setBufferedToImage(true)` cache; no additional reference source was copied for caching.

Open-Q's continuous editor resizing and width-scaled card/control layout follow the Scale-mode architecture studied in the second reference's `PluginEditor.cpp`, `panel/main_panel.cpp` and `panel/control_panel/` at the pinned revision above. The implementation retains Open-Q's layout, uses JUCE's standard corner/constrainer, and saves per-instance dimensions instead of importing the reference's global UI settings APVTS/file helper. No new reference code or assets were copied for resizing.

## Pamplejuce template

The original build system, plugin skeleton, and supporting template files are derived from [Pamplejuce](https://github.com/sudara/pamplejuce), copyright (c) 2022 Sudara Williams. Its complete MIT notice is retained in [licenses/Pamplejuce-MIT.txt](licenses/Pamplejuce-MIT.txt).

## Framework and dependencies

[JUCE](https://github.com/juce-framework/JUCE), melatonin_inspector, clap-juce-extensions, Catch2, and their bundled dependencies retain their own license files and copyright notices in the corresponding submodules or fetched source directories. Open-Q's project license does not replace those notices.

## Level-bar and gain-control implementation references

Open-Q's meter rendering was informed by Grisey revision `5e315049a981472fca31a84efd23778e1ff2d253` (`Source/Views/LevelMeters.*`, `Source/UI/CachedLayer.h`, `Source/Engine/MeterEngine.h`, `AtomicMax.h`), declared GPLv3 in its README and `LICENSE.md`. Its README credits Yulania and identifies upstream <https://github.com/ZhiyuAlexZhang/Grisey>; the studied local clone is Yohan's fork. Open-Q adapts `Source/Engine/AtomicMax.h` into `source/dsp/PeakLatch.h`, retaining GPL-3.0-only coverage and the full license at `licenses/Grisey-GPL-3.0.txt`. The adaptation adds finite/nonnegative validation and replaces the weak-CAS retry loop with one strong compare/exchange and a reset-only fallback store under a single-producer contract. The GPL-covered helper remains GPL-covered in the combined AGPLv3 Open-Q work, as permitted by GPLv3 section 13. Open-Q separately implements the meter UI's static/dynamic layers, pixel-based repaint suppression and slow numeric readouts; Grisey artwork, RMS/loudness engine and cache helper were not imported.

ZLEqualizer revision `3ce598ee065b3d3847121e76e196feda7ed8f56d` (copyright (C) 2026 zsliu98, AGPLv3) also informed static/dynamic meter separation and gain interaction through `panel/curve_panel/meter_panel/`, `panel/top_panel/output_label.cpp`, `gui/slider/compact_linear_slider/` and `gui/attachment/slider_attachment.hpp`. These components were not imported. Open-Q uses its own sample-peak renderer and normal JUCE APVTS attachments. Its 20 ms fixed-duration gain ramps use JUCE's existing SmoothedValue utility and intentionally differ from ZLE's fixed linear-amplitude slew. No new dependencies or reference logos were added.

## ZLEqualizer preset browser port

- Author/copyright: **Copyright (C) 2026 zsliu98**.
- Pinned source: ZLEqualizer `3ce598ee065b3d3847121e76e196feda7ed8f56d`.
- Imported/adapted sources: `source/panel/preset_browser/` browser, group/preset lists, entry/layout helpers, rounded editor and JSON codec; `source/gui/scrolling/` virtualized list, styled scrollbar and scroll model; `source/panel/helper/paint_selected_card.hpp`.
- Open-Q destination: `source/ui/presets/`, under **AGPL-3.0-only**. Original license/copyright blocks are retained, including Spectrum project names present in several upstream files. `warning_overlay.*` is adapted to the same embedded confirmation workflow using Open-Q/JUCE controls.
- Modifications by Yohan Senanayake, 28 October 2026: replace ZLE application/UI-state/theme dependencies with a small Open-Q style bridge, existing icon buttons and an opaque bounded panel; lazy editor ownership and header integration; synchronous scanning retained by user choice, with symlink traversal disabled; remove ZLE-specific legacy-folder migration; use Open-Q's user-library directory and JSON format marker; serialize bands/trims while preserving live bypass/display preferences on load; shared result-returning processor validation, bounded preset input/tree depth, and temporary-file replacement on save. Confirmation and select-versus-double-click-load behaviour are retained. No excluded logos, additional assets or dependencies were imported.

## Release validation tools

Release CI builds [Tracktion pluginval](https://github.com/Tracktion/pluginval) v1.0.4, commit `ed19c2c16b57a6d94db391bea3ef4a80b769d5bf`, natively on Linux/Windows ARM64 because the published x86-64 validator cannot load ARM64 plugins. Its upstream GPLv3 notices and pinned JUCE submodule are retained in the CI source checkout. Other jobs use the existing v1.0.4 binaries. The validator is a build/test tool and is not bundled with Open-Q.
