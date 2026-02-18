cmake_minimum_required(VERSION 3.15)

project(SongbirdPlayer VERSION 1.0.0)

# macOS deployment target
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE INTERNAL "")
endif()

# Add JUCE from tracktion's bundled copy
add_subdirectory(tracktion/modules/juce)

# Add tracktion modules
add_subdirectory(tracktion/modules)

# IDE source groups
set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

# Create the standalone GUI app
juce_add_gui_app(SongbirdPlayer
    BUNDLE_ID "com.songbird.player"
    PRODUCT_NAME "Songbird Player"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio output"
    PLUGINHOST_AU TRUE)

juce_generate_juce_header(SongbirdPlayer)

target_compile_features(SongbirdPlayer PRIVATE cxx_std_20)

# Source files
target_sources(SongbirdPlayer PRIVATE
    app/Main.cpp
    app/SongbirdEditor.cpp)

# Compile definitions
target_compile_definitions(SongbirdPlayer PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SongbirdPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SongbirdPlayer,JUCE_VERSION>")

# Link libraries
target_link_libraries(SongbirdPlayer PRIVATE
    tracktion::tracktion_engine
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags)

# macOS-specific
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(SongbirdPlayer PRIVATE "-fno-aligned-allocation")
    target_link_libraries(SongbirdPlayer PRIVATE
        "-framework WebKit")
endif()
