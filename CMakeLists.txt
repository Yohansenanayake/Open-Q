cmake_minimum_required(VERSION 3.25)

# This tells cmake we have goodies in the /cmake folder
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include (PamplejuceVersion)

# Modern concise way to add dependencies to your project
include (CPM)

# Configures universal binaries and decides which version of macOS to support
# The template forces universal CI builds; preserve explicit release architectures.
set(openq_requested_osx_architectures "${CMAKE_OSX_ARCHITECTURES}")
include(PamplejuceMacOS)
if(openq_requested_osx_architectures)
    set(CMAKE_OSX_ARCHITECTURES "${openq_requested_osx_architectures}")
endif()

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# Change me!
# This is the internal name of the project and the name of JUCE's shared code target
# Note: This cannot have spaces (it may be 2024, but you can't have it all!)
# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs display)
set(PROJECT_NAME "OpenQ")

# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs will display)
# You can also just have it be the same thing as PROJECT_NAME
# You may want to append the major version on the end of this (and PROJECT_NAME) ala:
#   set(PROJECT_NAME "MyPlugin_v${MAJOR_VERSION}")
# Doing so enables major versions to show up in IDEs and DAWs as separate plugins
# allowing you to change parameters and behavior without breaking existing user projects
set(PRODUCT_NAME "Open-Q")

# Change me! Used for the MacOS bundle name and Installers
set(COMPANY_NAME "Yohan Senanayake")

# Change me! Used for the MacOS bundle identifier (and signing)
set(BUNDLE_ID "com.yohansenanayake.openq")

# Change me! Set the plugin formats you want built
# Valid choices: AAX Unity VST VST3 AU AUv3 Standalone
set(FORMATS Standalone VST3)
if(APPLE)
    list(APPEND FORMATS AU AUv3)
endif()

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

# For simplicity, the name of the CMake project is also the name of the target
project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/OpenQSimd.cmake")

# Log build environment info (after project() so compiler info is available)
include(PamplejuceLog)

# Defines the pamplejuce_* functions we call below (also fetches Catch2 at include time)
include(PamplejuceFunctions)

# JUCE is setup as a submodule in the /JUCE folder
# Locally, you must run `git submodule update --init --recursive` once
# and later `git submodule update --remote --merge` to keep it up to date
# On Github Actions, this is done as a part of actions/checkout
add_subdirectory(JUCE)

# Add CLAP format
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add any other modules you want modules here, before the juce_add_plugin call
# juce_add_module(modules/my_module)

# This adds the melatonin inspector module
add_subdirectory (modules/melatonin_inspector)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Icons for the standalone app
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    # Change me!
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"

    # Keep build output local; load the VST3 explicitly in AudioPluginHost.
    COPY_PLUGIN_AFTER_BUILD FALSE

    # Change me!
    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE Ysen

    # Change me!
    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE OpQ1
    FORMATS "${FORMATS}"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES EQ

    # The name of your final executable
    # This is how it's listed in the DAW
    # This can be different from PROJECT_NAME and can have spaces!
    # You might want to use v${MAJOR_VERSION} here once you go to v2...
    PRODUCT_NAME "${PRODUCT_NAME}")

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

# Analyzer dependencies match ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d.
block(SCOPE_FOR VARIABLES POLICIES)
    # The pinned upstream CMakeLists explicitly sets CMP0111 OLD.
    if(POLICY CMP0218)
        cmake_policy(SET CMP0218 OLD)
    endif()
    set(CMAKE_WARN_DEPRECATED OFF)
    CPMAddPackage(NAME highway
        GITHUB_REPOSITORY ZL-Audio/highway
        GIT_TAG 2607d3b5b0113992fe84d3848859eae13b3b52c1
        OPTIONS "HWY_ENABLE_TESTS OFF" "HWY_ENABLE_EXAMPLES OFF" "HWY_ENABLE_CONTRIB OFF" "HWY_COMPILE_ONLY_STATIC ON")
    openq_verify_simd("${highway_SOURCE_DIR}")
endblock()
CPMAddPackage(NAME zldsp_fft
    GITHUB_REPOSITORY ZL-Audio/zldsp_fft
    GIT_TAG dfc50e6c39680d9ee04fbb60fc6c935552630045
    DOWNLOAD_ONLY YES)

# Default builds follow the compiler baseline; release jobs select explicit SIMD targets.
target_compile_definitions(SharedCode INTERFACE HWY_COMPILE_ONLY_STATIC)
target_include_directories(SharedCode SYSTEM INTERFACE "${zldsp_fft_SOURCE_DIR}/src")
target_link_libraries(SharedCode INTERFACE hwy)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES audio-effect)

# Apply C++23 and the template compiler defaults
pamplejuce_shared_code_defaults()

# Keep finite-value validation effective in optimized DSP builds.
get_target_property(sharedCodeOptions SharedCode INTERFACE_COMPILE_OPTIONS)
list(FILTER sharedCodeOptions EXCLUDE REGEX "Ofast|fp:fast")
set_property(TARGET SharedCode PROPERTY INTERFACE_COMPILE_OPTIONS "${sharedCodeOptions}")

# Manually list all .h and .cpp files for the plugin
# If you are like me, you'll use globs for your sanity.
# Just ensure you employ CONFIGURE_DEPENDS so the build system picks up changes
# If you want to appease the CMake gods and avoid globs, manually add files like so:
# set(SourceFiles Source/PluginEditor.h Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginProcessor.cpp)
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Adds a BinaryData target for embedding assets into the binary
pamplejuce_add_assets()

# MacOS only: Cleans up folder and target organization on Xcode.
pamplejuce_xcode_prettify()

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    # JUCE_WEB_BROWSER and JUCE_USE_CURL off by default
    JUCE_WEB_BROWSER=0  # If you set this to 1, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Uncomment if you are paying for a an Indie/Pro license or releasing under GPLv3
    # JUCE_DISPLAY_SPLASH_SCREEN=0

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    # JucePlugin_Name is for some reason doesn't use the nicer PRODUCT_NAME
    PRODUCT_NAME_WITHOUT_VERSION="${PRODUCT_NAME}"
)

# Link to any other modules you added (with juce_add_module) here!
# Usually JUCE modules must have PRIVATE visibility
# See https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md#juce_add_module
# However, with Pamplejuce, you'll link modules to SharedCode with INTERFACE visibility
# This allows the JUCE plugin targets and the Tests target to link against it
target_link_libraries(SharedCode
    INTERFACE
    Assets
    melatonin_inspector
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# Start with JUCE DSP; reconsider IPP after profiling.
# include(PamplejuceIPP)

# Everything related to the tests target
pamplejuce_add_tests()

# A separate target for Benchmarks (keeps the Tests target fast)
pamplejuce_add_benchmarks()

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
