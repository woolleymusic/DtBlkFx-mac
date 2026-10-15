cmake_minimum_required(VERSION 3.22)

# must come before project(), or CMake has already created an empty cache entry and the plug-in
# gets the SDK's own version as its minimum macOS
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(DtBlkFx VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Windows (MSVC; JUCE 9 does not support MinGW): static runtime for every target, so testers
# need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: the installed package (~/JUCE/install) for native macOS builds; the source tree
# (~/JUCE, or -DJUCE_SOURCE_DIR=...) for Windows and cross builds.
set(JUCE_SOURCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "JUCE source tree")
if(WIN32 OR CMAKE_CROSSCOMPILING)
    add_subdirectory("${JUCE_SOURCE_DIR}" JUCE)
else()
    find_package(JUCE 9.0.2 CONFIG REQUIRED HINTS "$ENV{HOME}/JUCE/install")
endif()

# --- FFTW 3 (single precision), linked statically so the plug-in has no FFTW dylib dependency.
# Preferred: the universal (arm64 + x86_64) build from tools/build_fftw_universal.sh in
# third_party/fftw-universal. Fallback on macOS: Homebrew's static library, which is arm64 only.
set(FFTW_UNIVERSAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fftw-universal")
if(APPLE AND EXISTS "${FFTW_UNIVERSAL_DIR}/lib/libfftw3f.a" AND NOT FFTW_ROOT)
    set(FFTW_ROOT "${FFTW_UNIVERSAL_DIR}")
    set(DTBLKFX_DEFAULT_ARCHS "arm64;x86_64")
    message(STATUS "DtBlkFx: universal FFTW from third_party/fftw-universal, building arm64 + x86_64")
elseif(APPLE AND NOT FFTW_ROOT)
    set(FFTW_ROOT "/opt/homebrew")
    set(DTBLKFX_DEFAULT_ARCHS "arm64")
    message(WARNING "DtBlkFx: third_party/fftw-universal not found, falling back to Homebrew FFTW "
                    "(/opt/homebrew, arm64 only). The plug-in will be Apple Silicon only. "
                    "Run tools/build_fftw_universal.sh for a universal build.")
endif()
set(FFTW_ROOT "${FFTW_ROOT}" CACHE PATH "FFTW install prefix (include/fftw3.h, lib/libfftw3f.a)")
find_path(FFTW_INCLUDE_DIR fftw3.h HINTS "${FFTW_ROOT}/include" NO_DEFAULT_PATH)
find_path(FFTW_INCLUDE_DIR fftw3.h HINTS "${FFTW_ROOT}/include" REQUIRED)
find_file(FFTW_STATIC_LIB NAMES libfftw3f.a fftw3f.lib HINTS "${FFTW_ROOT}/lib" NO_DEFAULT_PATH)
find_file(FFTW_STATIC_LIB NAMES libfftw3f.a fftw3f.lib HINTS "${FFTW_ROOT}/lib" REQUIRED)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "${DTBLKFX_DEFAULT_ARCHS}" CACHE STRING "" FORCE)
endif()
if(APPLE)
    message(STATUS "DtBlkFx: FFTW ${FFTW_STATIC_LIB}, architectures ${CMAKE_OSX_ARCHITECTURES}")
endif()

if(APPLE)
    set(DTBLKFX_FORMATS AU VST3 Standalone)
else()
    set(DTBLKFX_FORMATS VST3 Standalone)
endif()

# --- The original DtBlkFx engine (Darrell Tam), kept as close to verbatim as possible.
add_library(DtBlkFxEngine STATIC
    Source/engine/DtBlkFx.cpp
    Source/engine/DtBlkFxGlobals.cpp
    Source/engine/FxRun1_0.cpp
    Source/engine/FxState1_0.cpp
    Source/engine/fft_frac_shift.cpp
    Source/engine/fftw_support.cpp
    Source/engine/misc_stuff.cpp
    Source/engine/NoteFreq.cpp
    Source/engine/PixelFreqBin.cpp
    Source/engine/rfftw_float.cpp
    Source/engine/sweep1_coeff.cpp
    Source/engine/sweep2_coeff.cpp
    Source/engine/sweep3_coeff.cpp
    Source/engine/sweep4_coeff.cpp
    Source/engine/sweep5_coeff.cpp)
target_include_directories(DtBlkFxEngine SYSTEM PUBLIC Source/engine "${FFTW_INCLUDE_DIR}")
target_compile_definitions(DtBlkFxEngine PUBLIC STEREO=1)
target_link_libraries(DtBlkFxEngine PUBLIC "${FFTW_STATIC_LIB}")
set_target_properties(DtBlkFxEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(DtBlkFxEngine PRIVATE /W0 /permissive)
else()
    # 2005-era code: silence warnings rather than touch the algorithm
    target_compile_options(DtBlkFxEngine PRIVATE -w -fno-strict-aliasing)
endif()

# Skin PNGs and factory presets from the original release (resources/stereo_*).
juce_add_binary_data(DtBlkFxAssets SOURCES
    resources/stereo_presets.txt
    resources/stereo_fx_bg.png
    resources/stereo_global_ctrl.png
    resources/stereo_hslider_bg_coarse.png
    resources/stereo_hslider_bg_fine.png
    resources/stereo_hslider_knob_coarse.png
    resources/stereo_hslider_knob_fine.png
    resources/stereo_splash.png)

set(DTBLKFX_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetIO.cpp
    Source/gui/Controls.cpp
    Source/gui/SgramModel.cpp
    Source/gui/SpectrogramView.cpp
    Source/gui/GlobalCtrlView.cpp
    Source/gui/FxCtrlView.cpp)

juce_add_plugin(DtBlkFx
    COMPANY_NAME "Woolley"
    BUNDLE_ID com.woolley.dtblkfx
    PLUGIN_MANUFACTURER_CODE Wool
    PLUGIN_CODE DtBk
    FORMATS ${DTBLKFX_FORMATS}
    PRODUCT_NAME "DtBlkFx"
    DESCRIPTION "FFT block effects (port of Darrell Tam's DtBlkFx)"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE   # install by hand, see RESTART_HERE.md
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(DtBlkFx PRIVATE ${DTBLKFX_SOURCES})

# Ad-hoc sign each finished bundle (after JUCE's own post-build steps, e.g. the VST3 moduleinfo),
# so "codesign -v" passes and Apple Silicon will load the unsigned-by-a-developer build.
if(APPLE)
    foreach(fmt ${DTBLKFX_FORMATS})
        add_custom_command(TARGET DtBlkFx_${fmt} POST_BUILD
            COMMAND codesign -s - --deep --force "$<TARGET_BUNDLE_DIR:DtBlkFx_${fmt}>"
            COMMENT "Ad-hoc signing DtBlkFx ${fmt}"
            VERBATIM)
    endforeach()
endif()

target_compile_definitions(DtBlkFx PUBLIC
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1   # host parameter IDs = original VST2 indices 0..43
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DtBlkFx
    PRIVATE
        DtBlkFxEngine
        DtBlkFxAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline harness: WAV in -> DtBlkFx with a parameter set -> WAV out, plus an editor snapshot.
juce_add_console_app(DtBlkFxHarness PRODUCT_NAME "DtBlkFxHarness")

target_sources(DtBlkFxHarness PRIVATE tools/Harness.cpp ${DTBLKFX_SOURCES})

target_compile_definitions(DtBlkFxHarness PRIVATE
    DTBLKFX_HEADLESS=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(DtBlkFxHarness
    PRIVATE
        DtBlkFxEngine
        DtBlkFxAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
