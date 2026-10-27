# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Open-Q Project Direction

- Build an open-source JUCE equalizer alternative to FabFilter Pro-Q, one small, complete step at a time.
- The primary reference is the local project at `/home/yohan/Projects/audio/ZLEqualizer`. Study the relevant implementation before developing each feature; do not port the whole plugin at once.
- The second reference is `/home/yohan/Projects/audio/ZLSpectrumEqualizer`, studied at commit `10388f0cb594c6a322f8652cd718aed28c2e2c39`. Consult [Spectrum UI and architecture research](yohan-docs/zl-spectrum-equalizer-ui-architecture-wiki.md) for selected-band controls, host parameter identity, analyzer transport, and future dynamic telemetry. Its spectral DSP is not a replacement for our IIR engine; its README also excludes logos from AGPLv3 coverage.
- Reusing reference code or dependencies is welcome. Record their origins, preserve required notices, give credit, and check the applicable licenses before incorporating them.
- The reference README identifies ZLEqualizer as AGPLv3 and excludes its logos from that license. Open-Q is AGPL-3.0-only; preserve ZLE and template notices when importing reference code.
- Keep audio processing realtime-safe and use the existing Pamplejuce build and test structure.
- Keep filter response type, coefficient-design algorithm, and processing state separate so future per-band modes and different bell/shelf designs remain possible. Start with one validated design; do not implement extra modes prematurely.
- Consult [filter-design research](yohan-docs/filter-design-research.md) before choosing or importing DSP. Pin the implementation revision and preserve saved-session behavior when adding modes.

## Project Setup

- Product: **Open-Q**; CMake target: `OpenQ`; author/manufacturer: **Yohan Senanayake**.
- Bundle ID: `com.yohansenanayake.openq`; manufacturer code: `Ysen`; plugin code: `OpQ1`. Keep these stable for host/session compatibility.
- IDE: VS Code with Ninja, CMake Tools, and Microsoft C/C++ (GDB on Linux).
- GitHub Actions builds Linux, macOS, and Windows. Intel IPP is disabled until profiling justifies it. Signing and notarization are deferred on both macOS and Windows.
- The project license is AGPL-3.0-only. The template MIT notice is retained in `licenses/Pamplejuce-MIT.txt`; imported DSP origins are recorded in `THIRD_PARTY_NOTICES.md`.
- 24 fixed Bell/Low Shelf/High Shelf/Low Cut/High Cut slots use ZLE-derived Ivantsov coefficients (`zle-ivantsov-v1` bell, `zle-ivantsov-shelf-v1` second-order shelves), independent double channel states, and 20 ms parameter smoothing and wet/dry fades. Parameter IDs are `band.0.frequency`, `band.0.gain`, `band.0.q`, `band.0.status`, `band.0.type`, `band.0.slope` through index 23. Type is Bell=0, Low Shelf=1, High Shelf=2, Low Cut=3, High Cut=4; default Bell. Status is Off=0 (unused slot), Bypass=1 (existing band without EQ contribution), or On=2. Band 1 starts On; the others start Off. Slot identity is independent of selection and frequency order.
- State schema 5 records all 144 parameters, the band count, and all three design revisions. The user authorized dropping compatibility with earlier prerelease states and automation; no migration is required. Validate complete state before replacing it. Preserve the pinned DSP design behavior when adding modes.
- The editor opens a compact `BandControlCard` on node selection, reattaching frequency/gain/Q knobs, the response-type selector, and the On/Bypass power button. Off is controlled by the trash/delete icon; the power button is disabled for Off slots. Power/trash/close SVGs and the formatting helper come from the pinned Spectrum reference, with notices retained; excluded logos are not imported. It starts hidden; deselection hides it without changing parameters. The trash button delegates deletion to the graph. The X button clears UI selection and hides the card without changing any band parameter; clicking the node reopens it. The card overlays the enlarged graph, avoids the selected node, and freezes placement during knob drags. Keep its current position whenever it is clear and within the usable graph area; only try fallback corners when relocation is needed. Band selection does not affect audio processing. Graph interaction is disabled during knob drags. Band selection uses nodes; there is no separate numbered slot row. Keyboard focus selects overlapping nodes, and background creation reuses Off slots. Nodes hide Off slots, dim Bypass, and share selection with the controls; background creation initializes frequency/gain/Q before On. Graph edits bracket host gestures and cancel them on interruption/teardown. Message-thread node positions read parameter atomics using the graph mapping; worker frames include unmuted per-band digital curves as well as the On-only sum. All statuses retain frequency/gain/Q/type/slope; Bypass fades dry but keeps coefficients, smoothing, and channel histories updating from the live input. Off fades dry, clears channel histories, and skips filtering. On at zero gain retains the tested fade-and-reset optimization. Band status is passed to DSP through `dsp/BandStatus.h`; existing parameter IDs remain unchanged; schema 5 adds per-band slope. Type and active cut-slope changes fade to dry over 20 ms, switch coefficients and reset history while dry, then fade back over 20 ms. Shelf nodes map gain to half-gain midpoint height and invert that mapping during drags. High shelf Q can create resonant peaks beyond the requested gain.
- Shared numeric policy: `dsp/BandLimits.h` owns band ranges/defaults and the effective filter frequency ceiling. `ui/ResponseMapping.h` owns independent graph dB limits and coordinate conversions; `ui/Palette.h` owns shared colors. Keep DSP precision and pinned formula constants intact. State child counts derive from `bandParameterNames`; slot count is 24.
- Cuts use `zle-ivantsov-cut-v1`, with fixed capacity eight sections per channel and independent histories. `CutSlope.h` maps 6/12/18/24/36/48/72/96 dB/oct to orders 1/2/3/4/6/8/12/16; default 12. The order-3 assembly uses a first-order section plus a biquad at sqrt(2)*user Q; even orders follow the pinned ZLE distribution. Clamp user Q once, never clamp derived section Q to the UI range. Cuts ignore gain and remain active at zero gain. Card gain is disabled for cuts, Q is disabled at 6 dB/oct, and slope is disabled for gain-based types; all inactive values are retained. Cut Q double-click resets to 1/sqrt(2) without changing the bell/shelf default of 1. Cut nodes sit at 0 dB with frequency-only gestures; first-order Q wheel is inactive. Background creation resets reused slots to Bell and the default slope before On.
- The graph uses an editor-owned response worker that reads APVTS parameter atomics and the processor's atomic prepared sample rate. Geometry and complete drawing frames travel through SPSC triple buffers; each mailbox has one producer and one consumer. Never read live filter state from the UI.
- The graph sums the On bands' target digital responses in dB using `makeBandCoefficients`, then clips the combined curve to the display range. It runs independently of `processBlock`, including while playback is stopped, and waits for a valid prepared sample rate rather than guessing one. Audio smoothing and wet/dry fades are intentionally not reflected in the curve.

- The processor owns `AnalyzerTransport`: a fixed-capacity packet SPSC FIFO with aligned pre/post samples, sample sequence/rate/layout and editor-session tags. One audio producer and one editor-owned `AnalyzerWorker` consumer; never reset its FIFO from the UI or preparation thread. Capture drops new samples when full and advances the source sequence, letting the consumer reset discontinuous history. No capture allocation, lock, wait or FFT in `processBlock`. The post tap must remain after the future global warm-bypass mix.
- Analyzer Pre/Post visibility (both default on) and Normal/High quality are processor-owned atomic display settings, saved as the optional `analyzerSettings` root property in schema 5; they do not add automatable parameters. Normal uses the scaled middle FFT (4096 at 44.1/48 kHz); High uses middle orders +2/0/−2 with ZLE's noise-power normalization and 200–400 / 2000–4000 Hz blends. FFT/Highway dependencies are pinned in CMake; imported smoothing/blending helpers retain ZLE notices. Stereo averages FFT channel powers, never waveforms or dB values. Default display: −96..0 dBFS, +4.5 dB/oct tilt at 1 kHz, 1/12-octave smoothing, 24 dB/s decay, roughly 30 Hz updates. Frames are coherent pre/post paths tagged with geometry, sample rate, session and settings. Resize preserves FFT history; stale input decays rather than repeatedly holding the same FFT. Workers/capture stop while the graph is hidden.

## About This Project

This project is derived from the [Pamplejuce](https://github.com/sudara/pamplejuce) template — a JUCE audio plugin template using CMake, C++23, and modern CI/CD. It builds cross-platform (macOS, Windows, Linux) with support for multiple plugin formats (VST3, AU, AUv3, CLAP, Standalone).

The template provides the build system, CI/CD, and project structure. The plugin-specific logic lives in `source/`.

## Build Commands

Initialize dependencies once: `git submodule update --init --recursive`.

```bash
# Debug development build (matches the VS Code tasks)
cmake -S . -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJUCE_BUILD_EXTRAS=ON
cmake --build Builds --config Debug --target OpenQ_VST3 OpenQ_Standalone AudioPluginHost --parallel 2

# Tests
cmake --build Builds --target Tests Benchmarks --parallel 2
ctest --test-dir Builds --verbose --output-on-failure

# Release build for making music / checking CPU usage
cmake -S . -B Builds/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJUCE_BUILD_EXTRAS=OFF
cmake --build Builds/Release --config Release --target OpenQ_VST3 OpenQ_Standalone --parallel 2

# Benchmarks
cmake --build Builds --target Benchmarks --parallel 2
./Builds/Benchmarks
```

Development defaults to **Debug**. Use **Release** for audio performance checks or if Debug causes high CPU or dropouts. VS Code's Run Build Task lists the Debug and Release tasks; F5 launches AudioPluginHost on Linux. Load `Builds/OpenQ_artefacts/Debug/VST3/Open-Q.vst3` in the host, then set plugin breakpoints.

## Project Structure

- `source/` - Plugin source code (PluginProcessor, PluginEditor)
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions, melatonin_inspector
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `PamplejuceFunctions.cmake` - One include that defines the `pamplejuce_*` functions below (and turns off their legacy include-time behavior)
- `PamplejuceIPP.cmake` - Intel IPP integration (optional)

Target setup happens via explicit function calls in `CMakeLists.txt`, not at include time:
- `pamplejuce_add_assets()` - Includes all files in assets/ as binary data (from `Assets.cmake`)
- `pamplejuce_add_tests()` - Configures the Catch2 test target (from `Tests.cmake`)
- `pamplejuce_add_benchmarks()` - Configures the Catch2 benchmark target (from `Benchmarks.cmake`)
- `pamplejuce_shared_code_defaults()` - C++23 and compiler defaults (from `SharedCodeDefaults.cmake`); Open-Q removes its fast-math flags to preserve finite-value checks
- `pamplejuce_xcode_prettify()` - Xcode folder/scheme cleanup (from `XcodePrettify.cmake`)

Note: after `include(PamplejuceFunctions)`, a plain `include(Tests)` (or Assets, etc.) is a no-op - call the function instead.

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (Standalone AU VST3 AUv3)
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: LSP/clangd often reports false positive diagnostic errors (like "undeclared identifier", "file not found") because it doesn't have full context of the JUCE module system. Ignore these unless the actual build fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_inspector](https://github.com/sudara/melatonin_inspector) — runtime component debugger (already included)
- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
