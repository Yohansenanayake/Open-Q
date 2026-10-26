# Open-Q

An open-source JUCE equalizer by **Yohan Senanayake**, developed incrementally as an alternative to FabFilter Pro-Q. Currently this is a personalized Pamplejuce starter with passthrough audio; EQ features are still to come.

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

- [Pamplejuce](https://github.com/sudara/pamplejuce) by Sudara Williams provides the build/test/CI template. Its MIT notice is retained in `LICENSE`.
- [JUCE](https://github.com/juce-framework/JUCE) provides the audio plugin framework; see its own license.
- [ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer) is the primary implementation reference, available locally at `/home/yohan/Projects/audio/ZLEqualizer`. No reference code has been imported yet. Record credits and license notices alongside any future reuse.

The project license is planned to switch to AGPL before incorporating AGPL reference code. The existing `LICENSE` is unchanged during initial setup.
