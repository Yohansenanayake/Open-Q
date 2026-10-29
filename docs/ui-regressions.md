# UI regression safeguards

Read the relevant entry before changing painting, controls, focus or native UI tests. These describe reproduced regressions and current safeguards, not a requirement to redesign unrelated code. [Agent guide](../CLAUDE.md) · [Architecture](architecture.md)

## Expensive spectrum paints starving controls

Meters originally refreshed independently. Native dirty-region merging repainted the underlying plot outside its budget; opaque footer/meters also clipped graph edges, defeating full-bounds paint-cost detection.

- Meter polling/painting must use the graph's admitted `onFrameRefresh` VBlank frames, not a separate animation timer.
- Measure visible plot paints, not full-component bounds or meter-only/partial hover damage. Reserve idle time after actual paint completion, including paints longer than the capped recovery estimate. Skip missed frames rather than catch up.
- Keep the editor timer for heartbeat/settings. Preserve the 200 ms stalled-gain-drag cancellation guard.
- Gain readout feedback may defer at most one eligible automatic graph/meter frame. Sustained input must not starve animation; direct interaction/expose paints remain immediate.
- Test per-frame input windows, not a short wall-clock paint-share percentage. Keep moving-meter stimulus separate from finite synthetic wheel bursts; explicit edits can force native redraws outside the automatic budget.

Tests: `tests/ResponseGraphTests.cpp`, `tests/GainMeterTests.cpp`. Local diagnosis: `yohan-docs/gain-meter-ui-stall-diagnosis.md`.

## Card repaint and gesture behavior

Rounded transparent cards allowed control-only damage to rerasterize the spectrum beneath them. Preserve `BandControlCard::OpaqueInterior`, mouse-transparent behind controls and inset by the shared radius. Outer corners stay transparent; do not mark the whole rounded card opaque. All eight control regions retain zero-underlying-plot-paint assertions. Opening/moving the card may still redraw the graph.

Disabling the card during a node press hid rotary value arcs and dimmed SVGs until mouse-up, exaggerating native input-delivery delays. Keep the card enabled and pointer access unchanged during node gestures. JUCE captures the ordinary mouse drag. Wheel-Q edits can run alongside frequency/gain drag gestures and must finish their own host gesture independently. The separate knob-drag graph lock remains. Do not add a general competing-pointer lock without a reproduced need.

Tests: `tests/NodeGraphTests.cpp`, `tests/ResponseGraphTests.cpp`.

## Focus and asynchronous selections

- Hiding a focused overlay can transfer focus to a node and reopen a card. Use the editor as neutral dismissal focus, clear selection on card close and use panel close callbacks for every dismissal path. Do not restore the previous card when closing Analyzer.
- ComboBox notifications are asynchronous. Refresh a selector only when its model field changes from the last displayed settings; unconditional timer writes can erase a pending selection. Unrelated Pre/Post edits must not overwrite it.
- macOS synthetic clicks do not activate AppKit like physical clicks. Test fixtures need a focusable peer, `Process::setDockIconVisible(true)` before activation, verified focus and asynchronous closure checks. Never activate the application from plugin controls to satisfy tests.
- Linux native-peer tests require Xvfb plus a ready Openbox window manager. Bare Xvfb can leave `WM_PROTOCOLS` unset and abort with `BadAtom`.

Tests: `GainMeterTests.cpp`, `AnalyzerConfigurationTests.cpp`, `NodeGraphTests.cpp`.

## Portable rendering and timing tests

- Destroy an image's `juce::Graphics` before pixel/BitmapData/PNG readback. Windows Direct2D completes the frame when the drawing context ends.
- Compare transparent compositing against the rendered underlay, not hardcoded native colour bytes; retain alpha and occlusion assertions.
- A tracked control paint cycle can have zero body duration because it was culled or finished below timer resolution; SVG children can still paint. Keep cycle observations without claiming they prove full redraw or physical presentation.
- Compare recorded numeric values with tolerances, not exact CSV decimal spellings. Value-initialize synthetic `MouseWheelDetails` and give events unique explicit timestamps; timer catch-up can repeat wall-clock timestamps.
- Continuous trim parameters must retain exact zero/unity through host normalization and recall. Preserve bit-exact dry coverage; do not loosen tests to hide snapping drift.

Tests: `ControlStyleTests.cpp`, `GainMeterTests.cpp`, `NodeGraphTests.cpp`, `ResponseGraphTests.cpp`.

## Unresolved Linux wheel delivery delay

With Fill enabled, captures showed old wheel events arriving in batches while plotting continued; gain callbacks and subsequent readout paints were fast. A local X11 probe reproduced a mechanism where selective paint-completion checks leave input inside Xlib while its socket is no longer readable. This has **not** established causality inside Open-Q.

The user deferred JUCE source patches pending Release testing. Do not pump native events from controls/painting or remove Fill/pacing to conceal the issue. Ctrl+Shift+T starts/stops `GainTimingTrace` and exports `Open-Q-gain-timing*.csv` to the system temporary directory. Its optional listeners measure gain/node event age, binding and paint cycles; avoid GDB pauses during captures. Local research/probes, when available: `yohan-docs/gain-input-timing-test.md` and `yohan-docs/x11-queued-input-probe.cpp`.
