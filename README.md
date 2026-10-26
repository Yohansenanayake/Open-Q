# Open-Q

An open-source JUCE equalizer by **Yohan Senanayake**, developed incrementally as an alternative to FabFilter Pro-Q. Open-Q provides eight mono/stereo bell EQ slots with custom ZLE-derived Ivantsov coefficients, DAW automation, saved state, and a combined frequency-response graph.

Select a slot with buttons 1–8, edit its frequency/gain/Q using the shared knobs, and toggle its enable control. Band 1 starts enabled; the others start disabled. Selection only changes the editing controls: all enabled bands process audio in slot order, each with independent channel histories. Disabled slots retain their settings.

Each band's frequency ranges from 20 Hz to 20 kHz (capped at 49% of sample rate), gain from -24 to +24 dB, and Q from 0.1 to 10. Controls smooth over 20 ms; enable changes also fade over 20 ms, clearing a fully dry band's ringing state. Double-click resets a knob. The default flat settings and all-disabled bank pass audio through unchanged. No buffering latency is added. Stacked boosts can produce very large levels; there is no output limiter. A conservative 40-second tail allowance lets hosts render extreme cascaded low-frequency ringing.

The graph shows the **combined target digital response** of all enabled bands using the same coefficient-design function as the audio DSP, on a logarithmic frequency axis and a -24 to +24 dB gain axis. Band responses add in dB before the combined display is clipped. A low-priority worker calculates 400 response points; triple buffers hand off geometry and completed drawing frames without making audio processing wait for the UI. The displayed range ends at Nyquist when it is below 20 kHz.

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

State schema 2 stores all eight slots and the bell design revision. Parameter IDs use `band.0.frequency`, `band.0.gain`, `band.0.q`, and `band.0.enabled` through index 7. Earlier development sessions/presets and `bell.*` automation are deliberately unsupported. New filter types, draggable graph nodes, and an analyzer remain future work; the coefficient-design function is separate from the channel-state processor. DSP uses ordinary floating-point semantics in optimized builds so finite-value checks remain effective.
