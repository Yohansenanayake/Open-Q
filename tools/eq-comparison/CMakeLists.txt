cmake_minimum_required(VERSION 3.25)
project(OpenQEqComparison LANGUAGES C CXX)

get_filename_component(OPENQ_ROOT "../.." ABSOLUTE)
add_subdirectory("${OPENQ_ROOT}/JUCE" JUCE EXCLUDE_FROM_ALL)

add_executable(EqComparison export.cpp "${OPENQ_ROOT}/source/dsp/FilterCoefficients.cpp")
target_compile_features(EqComparison PRIVATE cxx_std_23)
target_include_directories(EqComparison PRIVATE "${OPENQ_ROOT}/source")
target_compile_definitions(EqComparison PRIVATE JUCE_USE_CURL=0)
target_link_libraries(EqComparison PRIVATE juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
