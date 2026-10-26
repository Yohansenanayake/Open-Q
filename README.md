# Open-Q

An open-source JUCE equalizer by **Yohan Senanayake**, developed incrementally as an alternative to FabFilter Pro-Q. Open-Q provides eight mono/stereo bell EQ slots with custom ZLE-derived Ivantsov coefficients, DAW automation, saved state, and a combined frequency-response graph.

Select a slot with buttons 1–8, edit its frequency/gain/Q using the shared knobs, and choose **Off**, **Bypass**, or **On** in its status selector. Off marks an unused slot; Bypass keeps an existing band without applying its EQ; On applies its EQ. Band 1 starts On and the others start Off. All statuses retain their settings, and all eight slots remain selectable in this initial UI. Selection only changes the editing controls: all On bands process audio in slot order, each with independent channel histories.

Each band's frequency ranges from 20 Hz to 20 kHz (capped at 49% of sample rate), gain from -24 to +24 dB, and Q from 0.1 to 10. Controls smooth over 20 ms; switching between On and either silent status fades over 20 ms. Bypass keeps the filter running internally on its current input, including coefficient updates and smoothing after knob changes, while passing dry audio once the fade completes. Returning to On fades in the continuously updated filter output. Off clears filter history after fading dry and then skips filtering; a flat On band also clears history once dry. Bypass therefore continues using DSP CPU even though it contributes no EQ to the audible signal. Double-click resets a knob. The default flat settings and bank with no On bands pass audio through unchanged. No buffering latency is added. Stacked boosts can produce very large levels; there is no output limiter. A conservative 40-second tail allowance lets hosts render extreme cascaded low-frequency ringing.

The graph shows the **combined target digital response** of all bands set to On using the same coefficient-design function as the audio DSP, on a logarithmic frequency axis and a -24 to +24 dB gain axis. Band responses add in dB before the combined display is clipped. A low-priority worker calculates 400 response points; triple buffers hand off geometry and completed drawing frames without making audio processing wait for the UI. The displayed range ends at Nyquist when it is below 20 kHz.

Graph nodes follow the selected band's own frequency and gain. Click a node to select it and update the shared controls; drag horizontally for frequency and vertically for gain. Shift-drag gives fine control. Ctrl/Command with left drag locks frequency; with right drag it locks gain. Motion while an axis is locked is consumed so unlocking does not replay it. Wheel over a node edits Q, with Shift for finer changes. Bypassed nodes remain dimmed and editable; Off nodes are hidden. The faint selected-band curve remains visible during bypass, while the combined curve excludes it.

Double-click the graph background to create a bell in the first Off slot, resetting its Q to 1 and initializing frequency/gain before setting On. All eight occupied slots prevent further creation. Alt/Option-click a node or press Space while it has focus to toggle bypass. Delete/Backspace or Alt/Option-right-double-click sets its slot Off, retaining settings and selecting the nearest remaining band. Click empty graph space to deselect. Slot buttons remain available to select overlapping nodes or edit Off slots. Node positions reflect automation and preset recall without modifying saved values; frequencies above the effective DSP limit are displayed clamped. Nodes wait for a prepared sample rate. Lasso/group editing, solo, dynamic nodes, and configurable shortcuts remain future work.

The graph updates when knobs or automation change, including with stopped playback or suspended audio processing. It needs a valid sample rate from `prepareToPlay`, but does not need a `processBlock` call. Before the host supplies a sample rate, it displays “Waiting for audio preparation”. The curve represents the settled EQ settings; audio smoothing, the wet/dry transition, and ringing are not visualized by this response curve. Graph work stops while the editor is hidden and resumes when it is shown.

## Development

Requires CMake 3.25+, Ninja, a C++23 compiler, and the JUCE platform development dependencies. For Linux debugging, install GDB and the VS Code Microsoft C/C++ extension; CMake Tools is also recommended.

```bash
git submodule update --init --recursive
cmake -S . -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJUCE_BUILD_EXTRAS=ON
cmake --build Builds --config Debug --target OpenQ_VST3 OpenQ_Standalone AudioPluginHost --parallel 2
cmake --build Builds --target Tests Benchmarks --parallel 2
ctest --test-dir Builds --verbose --output-on-failure
```

VS Code **Run Build Task** offers `Build plugin binaries (Debug)` and `Build plugin binaries (Release)`. **F5** uses `VST3: JUCE AudioPluginHost (Linux)`, builds the VST3 and host, then launches the host under GDB. In AudioPluginHost, scan/load `Builds/OpenQ_artefacts/Debug/VST3/Open-Q.vst3`; breakpoints become active when the plugin loads.

Release builds use `Builds/Release`, keeping Debug available for development. Use Release when making music or measuring CPU usage. Builds stay in the project directory rather than installing plugins automatically.

GitHub Actions builds and tests on Linux, macOS, and Windows and runs pluginval. Signing and notarization are deferred; artifacts are unsigned (ZIP on Linux/macOS, installer on Windows). Intel IPP is disabled initially; revisit it when profiling identifies a useful optimization. The three-platform matrix consumes CI minutes on private repositories; reduce the matrix during active development if needed.

## References and credits

- [Pamplejuce](https://github.com/sudara/pamplejuce) by Sudara Williams provides the build/test/CI template. Its MIT notice is retained in `licenses/Pamplejuce-MIT.txt`.
- [JUCE](https://github.com/juce-framework/JUCE) provides the audio plugin framework; see its own license.
- [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer) is the primary implementation reference, available locally at `/home/yohan/Projects/audio/ZLEqualizer`. Its pinned Ivantsov bell coefficient calculation is adapted in `source/dsp/BellCoefficients.cpp`; see [third-party notices](THIRD_PARTY_NOTICES.md) for the source revision, author, and modifications.

Open-Q is licensed under **AGPL-3.0-only**; see [LICENSE](LICENSE). Original third-party notices are preserved.

State schema 3 stores all eight slots and the bell design revision. Parameter IDs use `band.0.frequency`, `band.0.gain`, `band.0.q`, and `band.0.status` through index 7. Earlier development sessions/presets and automation using `bell.*` or `band.*.enabled` are deliberately unsupported. Status values are Off=0, Bypass=1, On=2; saved status values must be integral and in range. New filter types and an analyzer remain future work; the coefficient-design function is separate from the channel-state processor. DSP uses ordinary floating-point semantics in optimized builds so finite-value checks remain effective.
