# Architecture reference

Task-specific implementation map, not a feature backlog. Confirm current values in the named source files and tests before editing. [Agent guide](../CLAUDE.md) · [Development](development.md) · [UI regressions](ui-regressions.md)

## Parameters and filter engine

`BandParameters.h` defines 24 fixed slots and the six per-band fields: frequency, gain, Q, status, type and slope. Slot identity is independent of UI selection and frequency order. `PluginParameters.h` derives the aggregate parameter count (currently 147), declares schema 8, trims and global bypass. Schema-7 sessions migrate to 1× oversampling after complete validation; schemas through 6 remain unsupported. Preserve enum values and host parameter order.

| Owner | Policy |
| --- | --- |
| `source/dsp/BandLimits.h` | Band ranges, defaults and effective frequency ceiling |
| `source/dsp/FilterCoefficients.cpp` | Pinned Ivantsov designs: `zle-ivantsov-v1`, `zle-ivantsov-shelf-v1`, `zle-ivantsov-cut-v1` |
| `source/dsp/BandFilter.*` | Per-channel double histories, 20 ms smoothing/fades and filter transitions |
| `source/dsp/CutSlope.h` | 6/12/18/24/36/48/72/96 dB/oct mapped to orders 1/2/3/4/6/8/12/16 |
| `source/ui/ResponseMapping.h` | Independent response-graph ranges and coordinate conversions |

Off=0, Bypass=1, On=2. All statuses retain settings. Bypass fades dry while processing live input and parameter updates; Off fades dry, resets histories and skips filtering. Flat gain-based bands retain the tested fade/reset optimization; cuts remain active at zero stored gain. Type and active cut-slope changes fade dry, switch/reset while dry, then fade back in.

Cuts use at most eight sections per channel. Order 3 is first-order plus a biquad at `sqrt(2) * userQ`; even orders retain the reference distribution. Clamp user Q once, never derived section Q. Cut gain is inactive; first-order cut Q is inactive. Cut Q reset is `1/sqrt(2)`, while bell/shelf Q defaults to 1. Shelf node height is half the requested gain; inverse drag mapping restores the full gain range.

See `BellFilterTests.cpp`, `ShelfFilterTests.cpp`, `CutFilterTests.cpp`, `MultibandTests.cpp` and `NodeGraphTests.cpp` in `tests/`.

## Trims, bypass and meters

`GainStage` uses continuous ±30 dB parameters and 20 ms linear-amplitude smoothing. Exact zero maps to exact unity. Global Total Bypass is the appended `global.bypass`, exposed through `getBypassParameter()`. A 20 ms fade mixes original input with the fully processed output while the wet engine and trims continue running. `processBlockBypassed` shares the forced bypass path without rewriting the parameter. Fixed-size chunks/member dry storage handle arbitrary host block sizes without allocation; advance the fade once per sample across channels, retaining exact endpoints.

IN/Pre follows input trim; OUT/Post follows the final global bypass mix. Thus bypassed Pre/Post traces can differ when input trim is nonzero. Curves and editing retain target settings; bypass dimming changes dynamic colours only.

`LevelMeterTransport` uses independent interval-maximum `PeakLatch` values. The editor is the sole consumer, distributing readings to both strips once per admitted graph frame; drain latches without animation when hidden. Capture is independent of analyzer visibility. Meter artwork is cached narrowly, bar/tick paints skip unchanged pixels and numeric maxima update at 10 Hz. Peaks are transient, not saved state. Tests: `GainMeterTests.cpp`, `GlobalBypassTests.cpp`.

## Oversampling and processing rates

`dsp/Oversampling.h` owns preallocated JUCE maximum-quality minimum-phase IIR 2×/4× converters and the raw-input delay ring. Default 1× uses the direct EQ path with zero added latency. Conversion surrounds the complete band bank; gains, global bypass fades, meters and analyzer packets stay at host rate. Bands use the multiplied sample rate for coefficient generation and 20 ms smoothing, while target frequency ceilings remain tied to host rate. Double coefficients/histories and pinned designs remain unchanged.

The session property `oversamplingMode` is 0/1/2 for 1×/2×/4×, independent of host automation parameters. The bottom-bar selector changes the requested quality. Library presets and Default preserve it; DAW state saves and restores it. Missing quality defaults to 1×. Presets from schema 7 remain loadable.

Live changes fade the final output to silence over 20 ms, publish a generation-tagged request, and continue processing without blocking. A processor-owned 30 Hz message-thread timer reports the new integer converter latency to the host and acknowledges the request; audio activates/reset-prepares the new mode while silent and fades in. Host preparation supersedes outstanding handoffs and applies the latest request directly. Once host notification is pending, that handoff finishes before another request is taken. A host may restart processing or briefly interrupt compensation. The message loop must run for live changes; requests made without callbacks remain pending. Non-realtime requests defer to preparation; a pending, unacknowledged live transition is cancelled if rendering becomes non-realtime.

Dry input is captured before both trims and delayed by the active mode's integer latency for global bypass, including the forced host-bypass callback. The wet engine stays warm and latency does not change on global bypass. Alignment removes bulk timing differences, not the converter's frequency-dependent phase. Mode changes intentionally permit history resets; ordinary band/global bypass transitions keep their existing policies.

`ProcessingConfiguration` publishes coherent host-rate/active-mode snapshots to UI workers without audio-thread locks or messages. Reads make one attempt and leave the caller's cached snapshot unchanged while a publication is busy. The graph retains its last valid configuration; the response worker defers the update without consuming queued geometry, then retries on the next refresh. Target response coefficients use the internal rate; axes/nodes use host rate. Curves describe the EQ target and exclude converter coloration. Future Pre/Post saturation belongs inside this single conversion pair; no saturation, placement control or dynamic EQ is implemented yet. Tests: `OversamplingTests.cpp`. This feature targets VST3 and Standalone; CLAP-specific latency/reactivation support is deferred. The existing CLAP target is not validated for live oversampling changes.

## Response and analyzer workers

The editor owns the response/analyzer workers. Response geometry and complete frames use SPSC triple buffers. The response worker reads parameter atomics and the processor's coherent prepared processing configuration, computes per-band target digital curves with the shared coefficient function and sums On bands in dB. It works without `processBlock`, including stopped playback, but waits for a prepared sample rate. It does not visualize smoothing/fades/ringing; UI code must not read live DSP histories.

The processor's fixed packet `AnalyzerTransport` has one audio producer and one editor-worker consumer. Aligned pre/post packets carry sequence, rate, layout and session tags. When full, drop new samples and advance the source sequence so the consumer detects discontinuities; never reset this FIFO from the UI/preparation thread. No FFT/capture allocation or wait occurs in audio processing.

Stereo analysis averages channel powers, not waveforms or dB. Normal uses the rate-scaled middle FFT (4096 at 44.1/48 kHz); High uses middle orders +2/0/−2 with the pinned normalization/blends. Keep dependency revisions in CMake unchanged during unrelated work.

`AnalyzerSettings` packs visibility/quality and smoothing/decay coherently. Defaults: both traces visible, Normal quality, half-ERB smoothing, remembered 1/12 octave, Medium decay, −96..0 dBFS and +4.5 dB/oct tilt at 1 kHz. Fill is an independent saved preference and does not reset analysis. Settings are non-automatable display state.

Temporal state lives in FFT bins. Apply tilt before decay; use actual elapsed worker time and the displayed minimum dB for decay calibration. Caller spectrum spans need not be SIMD-aligned: use Highway `LoadU`/`StoreU` for those buffers; only owned aligned history may use aligned operations. Resize preserves FFT history. After 250 ms without fresh input, provide a silent target so the display decays. Session/rate/quality/visibility changes reset appropriate histories. Stop worker/capture activity when hidden. Future adjustable analyzer ranges must feed the same decay calibration independently of the EQ gain axis. Tests: `AnalyzerTests.cpp`, `AnalyzerConfigurationTests.cpp`, `ResponseGraphTests.cpp`.

## Editor and controls

The graph spans behind `HeaderBar`, `BottomBar` and meter strips, with content insets reserving plot/card space. Layer order: background/spectrum, grid/labels, response curves, nodes, band card. `GraphArtwork` buffers bounded labels only; grid lines draw directly. Branding uses one parsed SVG and a small buffered child, invalidated on size/appearance changes.

`UiScale.h` derives moderated width-based region scales from `EditorSize.h`; additional height increases plot space. Keep fonts and bounds on the same region scale. Host/editor transforms stay untouched. Resize dismisses menus and cancels node gestures, while active knob gestures complete normally. Validated coherent processor-owned dimensions survive editor/session reopening.

Selection reattaches the compact band card to stable slot parameters; it does not change processing. Off slots hide nodes and are reused on creation, initializing Bell/default slope/frequency/gain/Q before On. Bypass nodes remain editable. Accents follow slot indices through `Palette.h`; the combined curve stays neutral. Preserve per-filter disabled controls and update card accents through attachments, not a timer.

Closing cards/panels must not change band state or reopen the previous selection. Keep card placement while clear/in bounds; relocate only when needed and freeze placement during knob drags. Embedded menus must dismiss on rebind/hide/teardown and release LookAndFeel references. Analyzer settings and preset browser are lazy-created; avoid populating JUCE's message-wakeup queue during host editor probing. See the [UI regression guide](ui-regressions.md) before changing these paths.

## Presets and state recall

`source/ui/presets/` adapts the pinned ZLE browser, lists, scrolling and JSON codec. Synchronous scanning/I/O is an explicit current choice, not an audio-thread operation. Scan on opening/library changes, search cached metadata, clear search on hiding while retaining group selection, and flush scrolling through admitted graph frames. Save through temporary-file replacement; confirm overwrite/deletion, bound input/tree depth and avoid symlink directory traversal.

Presets store band settings and trims while preserving bypass/oversampling/analyzer/editor size on load. Default resets sound parameters from their declared defaults, preserving those display/bypass/quality preferences. The library lives under user application data/manufacturer/plugin/Presets. State replacement goes through complete validation in `restoreStateInformation`.

The processor stores optional `presetName` session metadata (trimmed single-line text, maximum 256 characters; absent means Default). Numeric-looking text is valid. Update the name only after successful load/save, not selection or ordinary edits. Recall restores the label without reading a preset file. Keep metadata locks outside audio; the existing editor timer refreshes only changed header text. Dirty tracking, program mapping, background preset I/O and gain matching remain unimplemented; add only when requested. Tests: `PresetBrowserTests.cpp`, `MultibandTests.cpp`, `GlobalBypassTests.cpp`.
