# Release SIMD policy follows ZLEqualizer 3ce598ee065b3d3847121e76e196feda7ed8f56d.
# DEFAULT preserves local builds and the universal macOS development/CI build.
set(OPENQ_SIMD_TARGET "DEFAULT" CACHE STRING "Static SIMD target: DEFAULT, SSE2, AVX2 or NEON")
set_property(CACHE OPENQ_SIMD_TARGET PROPERTY STRINGS DEFAULT SSE2 AVX2 NEON)

if(NOT OPENQ_SIMD_TARGET MATCHES "^(DEFAULT|SSE2|AVX2|NEON)$")
    message(FATAL_ERROR "Unsupported OPENQ_SIMD_TARGET: ${OPENQ_SIMD_TARGET}")
endif()

if(NOT OPENQ_SIMD_TARGET STREQUAL "DEFAULT")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" openq_simd_arch)
    # Windows ARM64 runners can host x64 CMake/Clang executables under emulation.
    if(CMAKE_CXX_COMPILER_TARGET)
        string(REGEX REPLACE "-.*" "" openq_simd_arch "${CMAKE_CXX_COMPILER_TARGET}")
        string(TOLOWER "${openq_simd_arch}" openq_simd_arch)
    endif()
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(openq_simd_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()

    if(OPENQ_SIMD_TARGET STREQUAL "NEON")
        if(NOT openq_simd_arch MATCHES "^(aarch64|arm64)$")
            message(FATAL_ERROR "NEON requires a single ARM64 architecture, got ${openq_simd_arch}")
        endif()
        # Do not require ARM crypto extensions for the baseline download.
        set(openq_hwy_target HWY_NEON_WITHOUT_AES)
        set(openq_simd_flags -march=armv8-a+simd)
    else()
        if(NOT openq_simd_arch MATCHES "^(amd64|x86_64)$")
            message(FATAL_ERROR "${OPENQ_SIMD_TARGET} requires a single x86-64 architecture, got ${openq_simd_arch}")
        endif()
        set(openq_hwy_target HWY_${OPENQ_SIMD_TARGET})
        if(OPENQ_SIMD_TARGET STREQUAL "AVX2")
            # Highway AVX2 also requires FMA, F16C, BMI2, AES and PCLMUL.
            set(openq_simd_flags -march=x86-64-v3 -maes -mpclmul)
        else()
            set(openq_simd_flags -march=x86-64)
        endif()
    endif()

    if(MSVC)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            list(TRANSFORM openq_simd_flags PREPEND "/clang:")
        else()
            message(FATAL_ERROR "Explicit release SIMD targets on Windows require clang-cl")
        endif()
    endif()

    # Apply the same ABI/target policy to the FFT, Highway and their callers.
    add_compile_options(${openq_simd_flags})
    add_compile_definitions(HWY_BASELINE_TARGETS=${openq_hwy_target})
endif()
message(STATUS "Open-Q SIMD policy: ${OPENQ_SIMD_TARGET} (${CMAKE_SYSTEM_PROCESSOR}; ${CMAKE_OSX_ARCHITECTURES})")

function(openq_verify_simd highway_source_dir)
    if(OPENQ_SIMD_TARGET STREQUAL "DEFAULT")
        return()
    endif()
    include(CheckCXXSourceCompiles)
    # This probe does not inherit SharedCode's C++23 target features.
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_REQUIRED_INCLUDES "${highway_source_dir}")
    set(CMAKE_REQUIRED_DEFINITIONS -DHWY_COMPILE_ONLY_STATIC -DHWY_BASELINE_TARGETS=${openq_hwy_target})
    string(JOIN " " CMAKE_REQUIRED_FLAGS ${openq_simd_flags})
    unset(OPENQ_SIMD_TARGET_VERIFIED CACHE)
    check_cxx_source_compiles("
        #include <hwy/detect_targets.h>
        static_assert(HWY_STATIC_TARGET == ${openq_hwy_target});
        int main() { return 0; }
    " OPENQ_SIMD_TARGET_VERIFIED)
    if(NOT OPENQ_SIMD_TARGET_VERIFIED)
        message(FATAL_ERROR "Compiler/Highway cannot select requested ${OPENQ_SIMD_TARGET} target")
    endif()
endfunction()
