# Building and developing Open-Q

[Back to Open-Q](../README.md) · [Releases and distribution](releasing.md)

## Requirements

- CMake 3.25+, Ninja and a C++23 compiler.
- Platform development tools: Xcode command-line tools on macOS; Visual Studio C++ tools on Windows (CI uses clang-cl); Clang or GCC on Linux.
- JUCE's native platform dependencies. The [build workflow](../.github/workflows/build_and_test.yml) lists the Linux packages used in CI.

Clone the repository and its pinned submodules:

```bash
git clone --recurse-submodules https://github.com/Yohansenanayake/Open-Q.git
cd Open-Q
```

For an existing checkout, run `git submodule update --init --recursive`. Run the following commands from the repository root. CMake fetches the pinned FFT and Highway dependencies automatically.

## Release build

```bash
cmake -S . -B Builds/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXTRAS=OFF
cmake --build Builds/Release --config Release --target OpenQ_VST3 OpenQ_CLAP OpenQ_Standalone --parallel 2
```

On Windows, use a Visual Studio developer terminal and add `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` to configuration. On macOS, `OpenQ_AU` is also available. AUv3 requires the Xcode generator and is not built by Ninja.

Binaries appear in `Builds/Release/OpenQ_artefacts/Release/`; they are not installed automatically. Use Release builds for music production and performance measurements. Explicit SIMD options and release architectures are documented in the [release guide](releasing.md); `OPENQ_SIMD_TARGET=DEFAULT` preserves the compiler baseline for local builds.

## Debugging

```bash
cmake -S . -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJUCE_BUILD_EXTRAS=ON
cmake --build Builds --config Debug --target OpenQ_VST3 OpenQ_Standalone AudioPluginHost --parallel 2
```

VS Code's Run Build Task offers Debug and Release tasks. On Linux, F5 launches JUCE AudioPluginHost under GDB. Load `Builds/OpenQ_artefacts/Debug/VST3/Open-Q.vst3` to debug the plugin. Install the Microsoft C/C++ and CMake Tools extensions for this workflow.

## Tests

```bash
cmake --build Builds --target Tests Benchmarks --parallel 2
ctest --test-dir Builds --verbose --output-on-failure
python3 -m unittest discover -s tools/tests
```

Native editor tests need a desktop session. Linux headless CI starts Xvfb and Openbox; see the workflow above. Benchmarks are registered separately, so build their target before running all CTest entries.

## Build-system notes

`CMakeLists.txt` owns targets, formats and pinned dependencies; `VERSION` owns the numeric plugin version. `SharedCode` is an INTERFACE target shared by plugin/test/benchmark targets. Template helpers in the `cmake/` submodule use explicit `pamplejuce_*` function calls after `include(PamplejuceFunctions)`; including `Tests` or `Assets` alone does not create targets in that mode. Keep product policy in `tools/cmake/` rather than editing the template submodule.

The template recursively embeds `assets/` as JUCE BinaryData. Large documentation-only images placed there may increase binary size. Retain source/license notices when organizing assets or dependencies.

## Code and UI configuration

- Band ranges/defaults: `source/dsp/BandLimits.h`; slot capacity: `source/BandParameters.h`.
- Graph coordinates: `source/ui/ResponseMapping.h`.
- Colours: `source/ui/Palette.h`; shared styling: `UiLookAndFeel`, `ControlDrawing.h` and `RotaryStyle.h`.
- Header logo/plaque: `source/ui/HeaderBar.h` and its implementation.

Rebuild and reopen the editor after changing colours or SVG artwork. The icon formatter is run from `assets/icons/` with `python3 ../../tools/format_svg_icons.py`; it processes SVGs in that directory.

Ctrl+Shift+T toggles the optional UI timing recorder and exports `Open-Q-gain-timing*.csv` to the system temporary directory. It measures input callbacks and completed paints, not physical screen presentation. Capture is off by default and shared across editors in a loaded plugin module.

Read [repository instructions](../CLAUDE.md) before changing DSP, state formats, thread handoffs or paint pacing. The [architecture reference](architecture.md) maps implementation areas; [UI regression notes](ui-regressions.md) explain the relevant safeguards and tests. Research and local diagnostic material live in `yohan-docs/`, which is intentionally not distributed in the repository.
