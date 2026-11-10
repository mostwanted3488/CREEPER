cmake_minimum_required(VERSION 3.22)

project(CREEPER VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# --- JUCE -----------------------------------------------------------------------------------
# Fetched automatically. To use a local copy: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin ---------------------------------------------------------------------------------
if(APPLE)
    set(CREEPER_FORMATS AU VST3 Standalone)
elseif(WIN32)
    set(CREEPER_FORMATS VST3 Standalone)
else()
    set(CREEPER_FORMATS VST3)
endif()

juce_add_plugin(Creeper
    PRODUCT_NAME "CREEPER"
    COMPANY_NAME "BUYORBEALOSER"
    COMPANY_WEBSITE "https://instagram.com/buyorbealoser"
    BUNDLE_ID "com.buyorbealoser.creeper"
    PLUGIN_MANUFACTURER_CODE Bobl
    PLUGIN_CODE Crpr
    FORMATS ${CREEPER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    DESCRIPTION "One-knob build-up riser")

target_sources(Creeper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

file(GLOB CREEPER_ASSETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.ttf")
juce_add_binary_data(CreeperAssets SOURCES ${CREEPER_ASSETS})
set_target_properties(CreeperAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(Creeper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    # local Linux test builds only
    target_compile_definitions(Creeper PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

set(CREEPER_JUCE_MODULES
    CreeperAssets
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics)
if(APPLE OR WIN32)
    list(APPEND CREEPER_JUCE_MODULES juce::juce_audio_utils)
endif()

target_link_libraries(Creeper
    PRIVATE
        ${CREEPER_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Offline test (not part of the release builds) ------------------------------------------
option(CREEPER_BUILD_TESTS "Build the offline test program" OFF)
if(CREEPER_BUILD_TESTS)
    juce_add_console_app(CreeperTest PRODUCT_NAME "CreeperTest")
    target_sources(CreeperTest PRIVATE
        Tests/HostTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(CreeperTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE)
        target_compile_definitions(CreeperTest PRIVATE JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    endif()
    target_link_libraries(CreeperTest PRIVATE
        CreeperAssets
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
