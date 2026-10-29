# Open-Q agent instructions

`AGENTS.md` is a symlink to this file. Edit `CLAUDE.md`; preserve the symlink.

## Project direction

- Build an open-source JUCE parametric EQ, incrementally. Complete the requested step without porting an entire reference plugin or adding speculative modes/features.
- For new DSP/UI features, study the relevant implementation in `/home/yohan/Projects/audio/ZLEqualizer` (primary) or `/home/yohan/Projects/audio/ZLSpectrumEqualizer` (secondary). Origins and pinned revisions are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Do not import their excluded logos.
- Preserve copyright/license headers when reusing code. Pin dependencies and record concise attribution. Open-Q is AGPL-3.0-only; its logo is separately reserved under [licenses/OpenQ-Logo.txt](licenses/OpenQ-Logo.txt).
- Do not update dependency/reference revisions incidentally. Keep product-specific CMake changes outside the `cmake/` template submodule.

## Read only what the task needs

| Work area | Reference |
| --- | --- |
| Building, debugging, tests, UI configuration | [docs/development.md](docs/development.md) |
| DSP, state, transport, analyzer or presets | [docs/architecture.md](docs/architecture.md), then the relevant source/tests |
| Painting, controls, focus, timing or native UI tests | [docs/ui-regressions.md](docs/ui-regressions.md) |
| CI, release versions, architectures or website downloads | [docs/releasing.md](docs/releasing.md) |

Source and tests are the authority for implementation details; update affected documentation when behavior changes. Local research/probes in `yohan-docs/` are gitignored and may be absent in other checkouts. Consult the relevant study when available, especially `filter-design-research.md` for coefficient-design work; do not load every wiki or invent missing evidence.

## Compatibility and DSP

- Product/target: **Open-Q / OpenQ**; manufacturer **Yohan Senanayake**; bundle `com.yohansenanayake.openq`; manufacturer/plugin codes `Ysen` / `OpQ1`. Keep host identity stable.
- Preserve parameter IDs/order, enum values, pinned coefficient designs and complete state validation. `BandParameters.h` owns slots/IDs; `PluginParameters.h` owns schema/global parameters. Derive counts from declarations, not copied literals.
- Keep filter response type, coefficient-design algorithm and processing state separate. Preserve double channel histories, parameter smoothing and tested type/slope transitions; keep formula constants/precision intact.
- Off retains settings but clears/skips filtering after fading out. Bypass retains live filter history and smoothing. Global bypass keeps the wet engine running, bypasses EQ and both trims audibly, and places OUT/Post taps after the final mix.
- Keep trim ranges continuous for exact unity. Do not introduce snapping that makes 0 dB slightly nonzero.
- Earlier schemas through 6 are already unsupported. That historical prerelease exception does not authorize dropping subsequent states or changing compatibility policy for a new task.

## Threading and realtime safety

- Audio processing must not allocate, lock, wait, perform file I/O or post UI/system messages. Preallocate fixed-capacity storage in construction/preparation. Do not call `AsyncUpdater` from the audio thread.
- Parameter listeners can run on the changing thread, including audio. Components and painting belong to the message thread; never read live filter state from UI code.
- Preserve one producer/one consumer per triple buffer or analyzer FIFO and editor-owned worker shutdown paths. The UI/preparation thread must not reset the audio-owned analyzer FIFO.
- `PeakLatch` requires a single nonzero audio writer, zero-exchanging UI consumption and always-lock-free floats. Preserve its bounded publication; do not add a retry loop or another producer.

## UI safeguards

- Automatic graph and meter refreshes share admitted VBlank frames through `GraphRefreshLimiter` / `onFrameRefresh`. Measure visible plot paint time even when overlays clip edges, and reserve idle time after actual paint completion. No independent meter animation timer.
- Keep control repaint requests local. Preserve the bounded gain-readout priority and stalled-drag guard; do not dispatch native events from controls or while painting.
- Cache bounded static regions, not the animated graph or a full-size transparent artwork layer. Keep the band card's opaque interior and transparent rounded corners.
- Use editor-child `CompactComboBox` menus, not desktop popup windows. Preserve teardown/menu dismissal and neutral-root focus when closing overlays. Create analyzer/preset panels lazily.
- Bracket edits in host gestures and cancel them on interruption/teardown. Keep the card enabled during node drags; do not restore a blanket pointer lock. Preserve the separate graph lock during knob drags and independent host gestures for wheel-Q edits.
- Use `Palette.h` and existing style/scale helpers for presentation. Global bypass dims dynamic traces/nodes/meters, not static artwork or whole-component alpha.

## Build and validation

Development defaults to Debug; use Release for CPU/audio-performance checks. Initialize submodules once with `git submodule update --init --recursive`.

```bash
cmake -S . -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJUCE_BUILD_EXTRAS=ON
cmake --build Builds --target Tests Benchmarks --parallel 2
ctest --test-dir Builds --output-on-failure
# For distribution/tooling changes:
python3 -m unittest discover -s tools/tests
```

Run checks appropriate to the change; build `Tests` before focused CTest cases and `Benchmarks` before all entries. Native UI tests need a desktop (Xvfb **and Openbox** in headless Linux CI). More build targets and platform compiler flags are in the development guide.

Use `.clang-format` (Allman braces, four spaces). Resolve compile warnings encountered during builds. Verify editor diagnostics against the real compiler/compile commands rather than changing working code solely for an LSP warning. Keep finite-value checks effective; do not reintroduce fast-math flags.

## Known unresolved issue

Linux wheel delivery can lag with Spectrum Fill. Captures suggest a JUCE/X11 queue issue, but causality inside Open-Q is unconfirmed. The user deferred JUCE patching: check Release behavior before reopening that work. Preserve Fill and paint pacing. Diagnostics: `source/ui/GainTimingTrace.h`, Ctrl+Shift+T capture, and local `yohan-docs/gain-input-timing-test.md` / `x11-queued-input-probe.cpp`. Read the UI regression guide before modifying related paths.
