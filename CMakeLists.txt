cmake_minimum_required(VERSION 3.22)
project(LoopCapture VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LOOPCAPTURE_CORE_TESTS_ONLY "Build portable concurrency tests without JUCE" OFF)
option(LOOPCAPTURE_BUILD_TESTS "Build tests" ON)
option(LOOPCAPTURE_COPY_PLUGIN "Copy plugin to JUCE's system plugin directory" OFF)
option(LOOPCAPTURE_STANDALONE "Also build a standalone routing test app" ON)
set(LOOPCAPTURE_JUCE_PATH "" CACHE PATH "Local JUCE 7/8 checkout; empty downloads JUCE")
set(LOOPCAPTURE_JUCE_TAG "7.0.12" CACHE STRING "Pinned JUCE tag (8.0.6 also supported)")

if(LOOPCAPTURE_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(CaptureCoreTests Tests/CaptureCoreTests.cpp)
    target_include_directories(CaptureCoreTests PRIVATE Source)
    target_link_libraries(CaptureCoreTests PRIVATE Threads::Threads)
    add_test(NAME CaptureCoreTests COMMAND CaptureCoreTests)
endif()
if(LOOPCAPTURE_CORE_TESTS_ONLY)
    return()
endif()

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()
if(NOT LOOPCAPTURE_JUCE_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/JUCE/CMakeLists.txt"
   AND LOOPCAPTURE_JUCE_TAG STREQUAL "7.0.12")
    set(LOOPCAPTURE_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/JUCE")
endif()
if(LOOPCAPTURE_JUCE_PATH)
    add_subdirectory("${LOOPCAPTURE_JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${LOOPCAPTURE_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()
if(LOOPCAPTURE_STANDALONE)
    list(APPEND PLUGIN_FORMATS Standalone)
endif()
# JUCE 7's manifest helper can emit trailing commas (invalid JSON). The
# manifest is optional: VST3 hosts discover the exported plugin factory.
# Also avoids invoking a Windows helper executable during cross-compilation.
juce_add_plugin(LoopCapture
    COMPANY_NAME "Cortez Audio"
    BUNDLE_ID "cl.cortezaudio.loopcapture"
    PLUGIN_MANUFACTURER_CODE Ncor
    PLUGIN_CODE Lcap
    PRODUCT_NAME "JUCE PLUGGIN"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/juce-pluggin-icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/juce-pluggin-icon.png"
    DESCRIPTION "Stereo real-time capture and sample export"
    FORMATS ${PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Tools
    VST3_AUTO_MANIFEST FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "JUCE PLUGGIN receives audio from your selected input device."
    COPY_PLUGIN_AFTER_BUILD ${LOOPCAPTURE_COPY_PLUGIN})
juce_generate_juce_header(LoopCapture)
juce_add_binary_data(LoopCaptureBranding
    NAMESPACE PluginBranding
    SOURCES Assets/juce-pluggin-icon.png
            Assets/Fonts/SpaceGrotesk.ttf
            Assets/Fonts/IBMPlexMono-Regular.ttf)
target_sources(LoopCapture PRIVATE
    Source/SystemAudio.h Source/SystemAudio.cpp
    Source/CaptureCore.h
    Source/PluginProcessor.h Source/PluginProcessor.cpp
    Source/PluginEditor.h Source/PluginEditor.cpp)
target_compile_definitions(LoopCapture PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(LoopCapture PRIVATE
    LoopCaptureBranding
    juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(WIN32)
    target_link_libraries(LoopCapture PRIVATE ole32 uuid)
endif()
if(MINGW)
    # Avoid requiring GCC/MinGW runtime DLLs on the user's Windows machine.
    target_link_options(LoopCapture_VST3 PRIVATE -static -static-libgcc -static-libstdc++)
    if(TARGET LoopCapture_Standalone)
        target_link_options(LoopCapture_Standalone PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(LOOPCAPTURE_BUILD_TESTS)
    juce_add_console_app(CaptureIntegrationTests PRODUCT_NAME "Capture integration tests")
    juce_generate_juce_header(CaptureIntegrationTests)
    target_sources(CaptureIntegrationTests PRIVATE Tests/CaptureIntegrationTests.cpp
        Tests/SystemAudioTests.cpp
        Source/SystemAudio.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(CaptureIntegrationTests PRIVATE Source)
    target_compile_definitions(CaptureIntegrationTests PRIVATE
        LOOPCAPTURE_TESTING=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(CaptureIntegrationTests PRIVATE
        LoopCaptureBranding
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME CaptureIntegrationTests COMMAND CaptureIntegrationTests)
    if(WIN32)
        target_link_libraries(CaptureIntegrationTests PRIVATE ole32 uuid)
    endif()
endif()
