cmake_minimum_required(VERSION 3.22)

project(NITEDRIVE VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon is the primary target, but shipping a universal binary costs
# nothing here and saves Intel users a separate download.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Use a JUCE checkout if one is already present (fast local iteration), otherwise
# fetch a pinned tag so CI and a fresh clone both work with no extra steps.
set(ND_JUCE_TAG "8.0.4" CACHE STRING "JUCE version to fetch when not supplied locally")

if(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
    message(STATUS "NITEDRIVE: using JUCE from $ENV{JUCE_PATH}")
    add_subdirectory("$ENV{JUCE_PATH}" ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "NITEDRIVE: using bundled JUCE")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
else()
    message(STATUS "NITEDRIVE: fetching JUCE ${ND_JUCE_TAG}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${ND_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# JUCE's recommended warning set includes two that fight this codebase rather than
# help it:
#   -Wfloat-equal    every "has this parameter changed since the last block" cache
#                    guard is an intentional exact comparison. An epsilon there would
#                    mean coefficients silently failing to update.
#   -Wswitch-enum    the DSP enums all carry a NumX sentinel that is deliberately not
#                    a case; plain -Wswitch still catches a genuinely missing one.
# This has to be an interface target linked after juce_recommended_warning_flags,
# because dependency options are appended after a target's own compile options.
add_library(nd_warning_tweaks INTERFACE)
target_compile_options(nd_warning_tweaks INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wno-float-equal -Wno-switch-enum>)

# Build-time version header: version from this file, git SHA from the checkout,
# regenerated every build so the stamp always matches the compiled commit.
set(ND_VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
set(ND_VERSION_HEADER "${ND_VERSION_HEADER_DIR}/Version.h")

add_custom_target(nd_version
    COMMAND ${CMAKE_COMMAND}
        -DND_VERSION=${PROJECT_VERSION}
        -DND_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/Source/Version.h.in
        -DND_OUTPUT=${ND_VERSION_HEADER}
        -DND_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${ND_VERSION_HEADER}
    COMMENT "Stamping version header")

set(ND_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ND_FORMATS AU)
endif()

juce_add_plugin(NITEDRIVE
    COMPANY_NAME            "Nitedrive Audio"
    BUNDLE_ID               "com.nitedriveaudio.nitedrive"
    PLUGIN_MANUFACTURER_CODE Ndrv
    PLUGIN_CODE             Ntdr
    FORMATS                 ${ND_FORMATS}
    PRODUCT_NAME            "NITEDRIVE"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         "Instrument" "Synth")

juce_generate_juce_header(NITEDRIVE)

set(ND_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp)

target_sources(NITEDRIVE PRIVATE ${ND_SOURCES})

target_compile_definitions(NITEDRIVE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_include_directories(NITEDRIVE PRIVATE Source ${ND_VERSION_HEADER_DIR})
add_dependencies(NITEDRIVE nd_version)



target_link_libraries(NITEDRIVE PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    nd_warning_tweaks)

# ---------------------------------------------------------------------------
# Offline DSP verification. Renders audio and measures it - see Tests/README.
# ---------------------------------------------------------------------------
enable_testing()

juce_add_console_app(nitedrive_tests PRODUCT_NAME "nitedrive_tests")

# The test app needs its own JuceHeader; the one generated for the plugin target is
# private to that target.
juce_generate_juce_header(nitedrive_tests)

target_sources(nitedrive_tests PRIVATE
    Tests/RunTests.cpp
    Source/Params.cpp
    Source/Presets.cpp)

target_compile_definitions(nitedrive_tests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_include_directories(nitedrive_tests PRIVATE Source)


target_link_libraries(nitedrive_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    nd_warning_tweaks)

add_test(NAME dsp_verification COMMAND nitedrive_tests)
