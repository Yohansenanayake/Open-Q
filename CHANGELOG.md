# Changelog

User-visible changes to Open-Q are recorded here. Entries under **Unreleased** are pending, not a published release. Full download versions, including beta suffixes, identify individual releases; the plugin's embedded numeric version is maintained in `VERSION`.

## [Unreleased]

## [0.1.0-beta.2] - 2026-10-29

First public beta candidate. Includes the features and known limitations listed under 0.1.0-beta.1, plus the changes below.

### Fixed

- Analyzer crashes in AVX2 builds caused by aligned SIMD writes to unaligned spectrum buffers.
- ARM64 release validation setup: install the Linux LADSPA headers, use native MSVC for Windows pluginval, and give the macOS SIMD configure probe an explicit C++ dialect.
- Windows ARM64 release checks failing to compile pluginval because its duration literals lacked the required `<chrono>` header.
- Preserve MSVC's exception-handling defaults when adding the Windows validator's header workaround.

### Added

- A **Full Build & Test** workflow that builds and tests all eight release variants. It can be started manually and also runs every night.
- Downloadable packages for testing before a release, plus diagnostic logs when a build fails.

## [0.1.0-beta.1] - 2026-10-29

Initial beta candidate; not published because release validation failed.

### Added

- 24 independent EQ bands with bell, low shelf, high shelf, low cut and high cut filters, using ZLE-derived Ivantsov coefficient designs.
- Cut slopes of 6, 12, 18, 24, 36, 48, 72 and 96 dB/octave, with frequency and Q controls where applicable.
- Smoothed parameter changes and band transitions. Per-band Off retains settings; Bypass keeps the filter processing internally for smooth comparisons.
- Interactive response graph with coloured, draggable nodes and floating band controls. Response curves update even when playback is stopped.
- Pre/post spectrum analysis with Normal/High quality, ERB/octave smoothing, adjustable decay and optional spectrum fill.
- Input/output gain controls with peak metering, exact value entry and mouse-wheel adjustment.
- Smooth global total bypass, including both gain trims, while keeping the wet processing engine running.
- Preset browser with groups and search, a Default reset, and preset-name recall with DAW sessions. Host automation and saved plugin state are supported.
- Resizable interface, mono/stereo processing and no added audio latency.
- Versioned release packaging for Windows, macOS and Linux, architecture-specific downloads, and SHA-256 checksums. Formats include VST3, CLAP and Standalone, plus AU on macOS.
- Website downloads for stable and preview releases, installation guidance, and reproducible analogue/Open-Q/JUCE/BLT response comparisons.
- Community code of conduct and public development documentation.

### Known limitations

- Windows and macOS downloads are not yet publisher-signed/notarized. Windows uses an installer; macOS and Linux use ZIP packages.
- On Linux, gain-wheel input can lag with Spectrum Fill enabled. The suspected JUCE/X11 event-delivery cause remains unconfirmed; Release-build behaviour needs further checking.

[Unreleased]: https://github.com/Yohansenanayake/Open-Q/compare/v0.1.0-beta.2...main
[0.1.0-beta.2]: https://github.com/Yohansenanayake/Open-Q/releases/tag/v0.1.0-beta.2
[0.1.0-beta.1]: https://github.com/Yohansenanayake/Open-Q/tree/v0.1.0-beta.1
