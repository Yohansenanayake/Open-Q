# Releases and distribution

[Back to Open-Q](../README.md) · [Development](development.md)

All commands below run from the repository root.


- **Build & Test** runs on branch pushes, external pull requests and manual dispatch. Same-repository pull requests use their branch-push run. It builds/tests Linux x86-64, Windows x86-64 and macOS universal binaries, validates VST3 with pluginval and uploads packages. It can also be called by the workflows below.
- **Full Build & Test** runs manually, or as a reusable workflow. It uses the same eight variants, tests, pluginval checks and packaging steps as Release, with read-only repository permissions. Validation artifacts use `0.1.0-verify.<run-id>.<attempt>` filenames; no tag or release is created.
- **Nightly** runs daily at 10:00 UTC on Open-Q's default branch, or manually. It calls Full Build & Test. Dependency revisions remain pinned.
- **Release** runs when a `v*` tag is pushed, or manually with an existing tag. Its numeric version must match `VERSION`: for example, `VERSION` containing `0.1.0` accepts `v0.1.0` or `v0.1.0-rc.1`. Commit the workflow and version changes before creating the tag. Manual runs also require the tag to exist remotely; they do not create tags.
- **GitHub Pages** rebuilds the storefront on website/tool changes to `main`, published-release changes and manual dispatch. It checks out the current default-branch website and generates static Stable and Preview/Beta downloads from published GitHub Releases metadata. Drafts are hidden; absent builds get no download link. The site uses the interface screenshots from `assets/images/`; updating them also triggers deployment. Its comparison viewer uses committed bell assets plus shelf/cut SVG and CSV exports from `tools/eq-comparison/results/`; updating those exports triggers deployment without rebuilding DSP or installing plotting dependencies. Support buttons link to `https://ko-fi.com/yohansenanayake`; the official Ko-fi cup is stored locally in `website/assets/`.

Release preparation checks out the tag, validates its version and passes the resolved commit and the tagged `.github/release-matrix.json` to the reusable build/test workflow. Only after every build/test/package job succeeds does it verify every expected package, generate `SHA256SUMS.txt` and create a **draft release** with generated release notes. A tag suffix (for example `-beta.1` or `-rc.1`) sets the prerelease designation; a bare numeric tag creates a stable candidate. Review the draft, downloads and installation behaviour, then publish it manually in GitHub. Only the draft job has `contents: write`. Build and publication concurrency groups are separate so reusable calls cannot cancel their parent release run.

Everyday CI keeps its three existing builds and artifact names. Tagged releases build the following native variants:

| Platform | Variants | Download |
| --- | --- | --- |
| Linux | `x86-64`, `x86-64-AVX2`, `arm64` | `Open-Q-<full-tag-version>-Linux-<variant>.zip` |
| Windows | `x86-64`, `x86-64-AVX2`, `arm64` | `Open-Q-<full-tag-version>-Windows-<variant>.exe` |
| macOS | `x86-64`, `arm64` | `Open-Q-<full-tag-version>-macOS-<variant>.zip` |

Release filenames preserve the complete tag version without `v`: `v0.1.0-beta.2` produces `Open-Q-0.1.0-beta.2-Windows-x86-64.exe`. `VERSION` and the plugin's embedded numeric version remain `0.1.0`. There are no versionless binary copies. All eight packages appear in that release's `SHA256SUMS.txt`.

The matrix is the source of both build jobs and the required checksum/package list. Each variant runs functional tests, benchmarks and VST3 pluginval. Linux/Windows ARM64 build the validator natively from pluginval v1.0.4 commit `ed19c2c16b57a6d94db391bea3ef4a80b769d5bf`; an x86-64 validator cannot load an ARM64 plugin. The source validator and its dependencies are test tools, not included in downloads. macOS uses the existing universal validator on native Intel/Apple Silicon runners. Windows ARM64 installers accept ARM64 systems and install native ARM64 binaries; users should install the architecture matching their DAW.

`OPENQ_SIMD_TARGET` is a CMake cache setting: `DEFAULT` preserves compiler-baseline/local/universal builds; release jobs explicitly use `SSE2`, `AVX2` or `NEON`. This policy is in `tools/cmake/OpenQSimd.cmake`, outside the template submodule. SSE2 is the conservative x86-64 option. AVX2 follows ZLE's x86-64-v3 plus AES/PCLMUL flags and also requires FMA, F16C and BMI2: choose the baseline download if unsure. ARM64 selects baseline NEON without requiring AES instructions. The build verifies the selected Highway static target; there is no runtime dispatch or automatic fallback in an AVX2 binary. These targets do not change EQ algorithms or parameter identity.

ZIPs include only plugin/application bundles, README and license/credit files. Ninja CI packages VST3, CLAP and Standalone, plus AU on macOS; AUv3 requires the Xcode generator and is not included. Prerelease suffixes appear in the tag and download filenames; the embedded binary version remains numeric. macOS installers, AAX, signing/notarization and compiling timing diagnostics out of public builds remain deferred. Hosted architecture builds and installation/DAW checks are required before claiming compatibility.

For local release-configuration checks, run `python3 -m unittest discover -s tools/tests`. After committing/pushing these changes, create a new candidate tag; old tags keep their original source and release matrix. The release request uses that existing tag and omits `target_commitish` to avoid the redundant workflow-permission check on an older SHA. No release is automatically published.

## Publishing and website updates

Keep pending user-visible changes in [CHANGELOG.md](../CHANGELOG.md) under **Unreleased**. When cutting a release candidate, move them into a dated section with the full version (for example `0.1.0-beta.1`) and leave a new Unreleased section above it. Commit this before tagging. Use that section to review and edit the draft's generated release notes before publication.

1. Set the numeric `VERSION` (for example `0.1.0`), prepare the changelog, then commit and push the changes to `main`. In Actions → **Full Build & Test** → **Run workflow**, select that branch and verify all eight variants pass. Download the validation artifacts for installation/DAW checks. A new commit needs another validation run.
2. Push a new tag on the validated commit: for example `v0.1.0-beta.2` for the next testing candidate, or `v0.1.0` for a stable candidate. Do not move an old tag to new code. Release still performs its own checks against the tagged commit.
3. Wait for Release to build/test the eight variants and prepare a draft containing versioned binaries and checksums.
4. Review the draft and manually publish it. This human publication triggers Pages; draft creation does not expose the binaries on the website.
5. Pages reads all release API pages during deployment and selects the most recently published release independently for each channel, using GitHub's prerelease flag and publication date. It renders the actual uploaded, nonempty, correctly versioned asset URLs into HTML. A preview-only project shows no stable download buttons; unpublished/deleted releases disappear after the next deployment. Release notes, checksums (when attached), installation guidance, older versions and preview feedback are linked alongside downloads. No visitor-side API calls, JavaScript or PAT is needed.

**One-time GitHub setup:** in Settings → Pages, select **GitHub Actions** as the build/deployment source. If the `github-pages` environment restricts deployment refs, allow the `main` branch and version tags matching `v*` (Settings → Environments → github-pages → Deployment branches and tags). Release-triggered runs have a tag ref even though they check out the latest website from the default branch. Keep Pages workflows/tools on `main` before publishing the first candidate.

Site-only edits deploy independently of plugin releases. After manual asset uploads or automation using `GITHUB_TOKEN`, run Pages manually if no release event fires; the intended pipeline publishes complete drafts manually. API errors fail deployment rather than replacing the site with empty release data. The site stays on its previous deployment if a build/deployment fails.

## First matrix failure and fixes

[Release run #5](https://github.com/Yohansenanayake/Open-Q/actions/runs/40902858354) exposed failures outside everyday CI's three configurations:

| Variant | Failure and correction |
| --- | --- |
| Linux x86-64 AVX2 | Analyzer decay wrote caller-owned, potentially unaligned spectra with aligned SIMD stores. Use unaligned stores for caller buffers; keep aligned stores for owned aligned history. A deliberately offset buffer reproduces the old crash and is now a regression test. |
| Linux ARM64 | Native pluginval lacked `ladspa.h`; install `ladspa-sdk` alongside the JUCE build dependencies. |
| Windows ARM64 | The pinned validator's older JUCE selects x86 inline assembly under ARM64 clang-cl. Build this test tool with native ARM64 MSVC, retaining clang-cl for Open-Q. |
| macOS ARM64 | The strict NEON configure probe failed. It did not inherit SharedCode's C++ dialect; explicitly select C++17 inside the probe. The subsequent Full Build & Test run passed this variant. |

Failed builds now upload CMake configure and CTest diagnostics. No dependency revisions or release tags are changed by these fixes.

[Full Build & Test run #1](https://github.com/Yohansenanayake/Open-Q/actions/runs/40916787820) passed seven variants. Windows ARM64 built Open-Q and passed its tests, then hit another pinned-pluginval portability issue: `BasicTests.cpp` uses `100ms` without including `<chrono>`, which MSVC's `<thread>` does not supply. The Windows validator build force-includes `<chrono>`, retaining native MSVC and the pinned dependency. Open-Q's compile flags and validation tests are unchanged.

[Run #2](https://github.com/Yohansenanayake/Open-Q/actions/runs/40921672432) confirmed that the header workaround compiled, but setting `CMAKE_CXX_FLAGS` directly had removed MSVC's default exception flag, `/EHsc`. The workaround now uses `CMAKE_CXX_FLAGS_INIT` so CMake preserves its platform defaults. A configure regression test verifies that the workflow's flags retain both the forced header and exception handling; it does not execute a Windows compiler. Confirm the validator build on the hosted runner before tagging.

To preview locally with fixture or downloaded API metadata:

```bash
gh api --paginate 'repos/Yohansenanayake/Open-Q/releases?per_page=100' --jq '.[]' | jq -s . > /tmp/openq-releases.json
python3 tools/build_downloads.py --releases /tmp/openq-releases.json --output _site
python3 -m http.server --directory _site 8000
```

An empty JSON array (`[]`) can be used to preview the no-release state without GitHub access. `website/index.html` is the design template; keep its `RELEASE_DOWNLOADS_START`/`RELEASE_DOWNLOADS_END` comment pair when editing the download layout. `_site/` is generated and ignored. Installation guidance lives in `website/install.html`.
