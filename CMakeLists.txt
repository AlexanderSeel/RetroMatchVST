cmake_minimum_required(VERSION 3.24)
project(RetroMatchSynth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RETROMATCH_COPY_PLUGIN "Copy plug-in into the user's plug-in folder after build" OFF)
set(RETROMATCH_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE source checkout")

if(RETROMATCH_JUCE_DIR AND EXISTS "${RETROMATCH_JUCE_DIR}/CMakeLists.txt")
  message(STATUS "Using local JUCE: ${RETROMATCH_JUCE_DIR}")
  add_subdirectory("${RETROMATCH_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

set(RETROMATCH_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND RETROMATCH_FORMATS AU)
endif()

juce_add_plugin(RetroMatchSynth
  COMPANY_NAME "AlAi Audio"
  COMPANY_WEBSITE "https://github.com/AlexanderSeel"
  BUNDLE_ID "audio.alai.retromatchsynth"
  PLUGIN_MANUFACTURER_CODE Alai
  PLUGIN_CODE Rms2
  FORMATS ${RETROMATCH_FORMATS}
  PRODUCT_NAME "RetroMatch Synth"
  PLUGIN_NAME "RetroMatch Synth"
  DESCRIPTION "Hybrid sample-to-synth matching instrument"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${RETROMATCH_COPY_PLUGIN})

juce_generate_juce_header(RetroMatchSynth)

target_sources(RetroMatchSynth PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/Engine/SynthEngine.cpp
    Source/Engine/ReferenceWavetable.cpp
  Source/Analysis/SampleAnalyzer.cpp
  Source/Matching/OfflineRenderer.cpp
  Source/Matching/SimilarityScorer.cpp
  Source/Matching/SoundMatcher.cpp)

target_compile_definitions(RetroMatchSynth PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RetroMatchSynth PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

option(RETROMATCH_BUILD_TESTS "Build DSP/matching smoke tests" OFF)
if(RETROMATCH_BUILD_TESTS)
  enable_testing()
  juce_add_console_app(RetroMatchTests PRODUCT_NAME "RetroMatch Tests")
  juce_generate_juce_header(RetroMatchTests)
  target_sources(RetroMatchTests PRIVATE
    Tests/SmokeTests.cpp
    Source/Engine/SynthEngine.cpp
    Source/Engine/ReferenceWavetable.cpp
    Source/Analysis/SampleAnalyzer.cpp
    Source/Matching/OfflineRenderer.cpp
    Source/Matching/SimilarityScorer.cpp
    Source/Matching/SoundMatcher.cpp)
  target_compile_definitions(RetroMatchTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
  target_link_libraries(RetroMatchTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  add_test(NAME RetroMatchSmoke COMMAND RetroMatchTests)
endif()
