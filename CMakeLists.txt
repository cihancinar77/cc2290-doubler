cmake_minimum_required(VERSION 3.22)
project(CC2290 VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE)

juce_add_plugin(CC2290
    COMPANY_NAME "CC Electronic"
    BUNDLE_ID com.ccelectronic.cc2290
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_STEP_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Cihn
    PLUGIN_CODE Db90
    FORMATS AU Standalone
    PRODUCT_NAME "CC2290")

target_sources(CC2290 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(CC2290 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(CC2290 PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(UISnap PRODUCT_NAME "UISnap")
target_sources(UISnap PRIVATE Source/UISnap.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(UISnap PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(UISnap PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
