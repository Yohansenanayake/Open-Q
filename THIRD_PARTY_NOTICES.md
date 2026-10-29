# Third-party notices

Open-Q contributions: copyright © 2026 Yohan Senanayake, [AGPL-3.0-only](LICENSE). Third-party material retains its original copyrights and licenses. The Open-Q logo is excluded from AGPL coverage; see [its separate notice](licenses/OpenQ-Logo.txt).

## ZL Audio code and assets

Copyright (C) 2026 zsliu98. Imported code/assets use **AGPL-3.0-only**; see [LICENSE](LICENSE). Original file headers are retained.

| Project and pinned revision | Used in Open-Q |
| --- | --- |
| [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer/tree/3ce598ee065b3d3847121e76e196feda7ed8f56d) — `3ce598ee065b3d3847121e76e196feda7ed8f56d` | Ivantsov coefficient designs (`FilterCoefficients.cpp`), triple buffer (`TripleBuffer.h`), spectrum helpers (`dsp/analyzer/reference/`) and preset browser/scrolling helpers (`ui/presets/`). |
| [ZL Spectrum Equalizer](https://github.com/ZL-Audio/ZLSpectrumEqualizer/tree/10388f0cb594c6a322f8652cd718aed28c2e2c39) — `10388f0cb594c6a322f8652cd718aed28c2e2c39` | `assets/icons/bypass.svg`, `close.svg`, `trash.svg` and `tools/format_svg_icons.py`. |

Open-Q adaptations by Yohan Senanayake, 26–28 October 2026, include API/validation changes, third-order cut assembly, allocator integration and preset/UI integration. The Spectrum icons/helper were copied unchanged. File-level origin and modification notices remain in the source. Both projects also informed UI interaction and threading patterns; their excluded logos were not imported.

Filter-design research: Yuriy Ivantsov, [On the Ideal Bilinear and Biquadratic Digital Filter](https://ivantsovy.com/research/paper1.pdf).

## FFT and SIMD dependencies

| Dependency and revision | License / retained text |
| --- | --- |
| [zldsp_fft](https://github.com/ZL-Audio/zldsp_fft/tree/dfc50e6c39680d9ee04fbb60fc6c935552630045) — `dfc50e6c39680d9ee04fbb60fc6c935552630045` | [Apache-2.0](licenses/zldsp_fft-Apache-2.0.txt). |
| [Highway (ZL Audio fork)](https://github.com/ZL-Audio/highway/tree/2607d3b5b0113992fe84d3848859eae13b3b52c1) — `2607d3b5b0113992fe84d3848859eae13b3b52c1` | Google LLC and contributors. Open-Q uses the Apache-2.0 option; [upstream license text](licenses/Highway-LICENSE.txt) also retains its BSD-3-Clause alternative and CC0 random-helper notice. |

These dependencies are used unchanged; their upstream source notices are retained.

## Grisey

[Grisey](https://github.com/ZhiyuAlexZhang/Grisey), credited upstream to Yulania, revision `5e315049a981472fca31a84efd23778e1ff2d253`. `Source/Engine/AtomicMax.h` was adapted into `source/dsp/PeakLatch.h`, retaining **GPL-3.0-only** coverage and the [full license](licenses/Grisey-GPL-3.0.txt).

Open-Q adds finite/nonnegative validation and single-attempt peak publication. Grisey also informed meter rendering. Its helper remains GPL-covered within the combined AGPLv3 work; it is not relicensed as AGPL.

## Template, framework and supporting libraries

| Project | License / notice location |
| --- | --- |
| [Pamplejuce](https://github.com/sudara/pamplejuce) | MIT; copyright (c) 2022 Sudara Williams. [Full notice](licenses/Pamplejuce-MIT.txt). |
| [JUCE](https://github.com/juce-framework/JUCE) | AGPLv3 option; [upstream framework notice](licenses/JUCE-LICENSE.md). Bundled-library texts are listed below. |
| [melatonin_inspector](https://github.com/sudara/melatonin_inspector) | MIT; copyright (c) 2021 Sudara Williams. [Full notice](licenses/melatonin_inspector-MIT.txt). |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | MIT; copyright 2019–2020 Paul Walker. [Full notice](licenses/clap-juce-extensions-MIT.txt). |
| Bundled CLAP / CLAP helpers | MIT; copyright (c) 2021 Alexandre BIQUE. [CLAP notice](licenses/CLAP-MIT.txt), [helpers notice](licenses/CLAP-helpers-MIT.txt). |
| [Catch2](https://github.com/catchorg/Catch2) (tests/benchmarks only) | Boost Software License 1.0; `LICENSE.txt` in its fetched source. |

Submodule revisions are pinned by Git; fetched dependency revisions are pinned in CMake. Their own licenses and bundled dependency notices remain applicable.

Release packages include unchanged JUCE dependency texts for [VST3](licenses/JUCE-VST3-MIT.txt), [AudioUnitSDK](licenses/JUCE-AudioUnitSDK-Apache-2.0.txt), [FLAC](licenses/JUCE-FLAC-BSD.txt), [Ogg Vorbis](licenses/JUCE-Ogg-Vorbis-BSD.txt), [JPEG](licenses/JUCE-JPEG-README.txt), [PNG](licenses/JUCE-PNG-LICENSE.txt), [zlib](licenses/JUCE-zlib-LICENSE.txt), [HarfBuzz](licenses/JUCE-HarfBuzz-COPYING.txt), [SheenBidi](licenses/JUCE-SheenBidi-Apache-2.0.txt), [LunaSVG](licenses/JUCE-LunaSVG-MIT.txt) and [PlutoVG](licenses/JUCE-PlutoVG-MIT.txt). This software is based in part on the work of the Independent JPEG Group.

## Website branding

`website/assets/kofi-cup.avif` is an unchanged Ko-fi cup image from the [official creator kit](https://more.ko-fi.com/brand-assets), used to link to the author's support page under [Ko-fi's promotional-use guidance](https://help.ko-fi.com/hc/en-us/articles/360007169493-Can-I-use-the-Ko-fi-logo-to-promote-my-page). Ko-fi branding remains its owner's property and is not relicensed under Open-Q's AGPL. This website-only image is not included in plugin bundles.

## Release validation tool

[Tracktion pluginval](https://github.com/Tracktion/pluginval), GPLv3, version 1.0.4 / commit `ed19c2c16b57a6d94db391bea3ef4a80b769d5bf`. Used for CI validation, including native ARM64 builds. Its source notices and JUCE dependency are retained in the CI checkout; the tool is not shipped with Open-Q.
