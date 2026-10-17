cmake_minimum_required(VERSION 3.22)
project(ChordGen VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ChordGen
    COMPANY_NAME "ChordGen"
    PLUGIN_MANUFACTURER_CODE Cgen
    PLUGIN_CODE Cgn1
    FORMATS VST3 Standalone
    PRODUCT_NAME "ChordGen"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChordGen PRIVATE
    Source/Engine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(ChordGen PRIVATE Source)

target_compile_definitions(ChordGen PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChordGen
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Engine unit tests (no JUCE needed):  cmake --build build --target engine_test
add_executable(engine_test Source/Engine.cpp Tests/engine_test.cpp)
set_target_properties(engine_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
