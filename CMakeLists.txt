cmake_minimum_required(VERSION 3.15)

project(SongbirdPlayer VERSION 1.0.0)

# Debug builds: use -O1 so the compiler can inline small functions and
# eliminate dead code.  Pure -O0 causes audio dropouts because the
# many compilation units each generate unoptimised call-frame overhead
# that the audio thread cannot tolerate at low buffer sizes.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O1" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_DEBUG   "-g -O1" CACHE STRING "" FORCE)
endif()

# macOS deployment target
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE INTERNAL "")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE INTERNAL "")
endif()

# Fetch libgit2 — in-process Git for zero-fork undo/redo
include(FetchContent)
FetchContent_Declare(libgit2
    GIT_REPOSITORY https://github.com/libgit2/libgit2.git
    GIT_TAG        v1.8.4
    GIT_SHALLOW    TRUE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(USE_SSH OFF CACHE BOOL "" FORCE)
set(USE_HTTPS OFF CACHE BOOL "" FORCE)
set(USE_BUNDLED_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libgit2)



# Add JUCE from tracktion's bundled copy
add_subdirectory(libraries/tracktion/modules/juce)

# Add tracktion modules
add_subdirectory(libraries/tracktion/modules)

# Third-party dependencies for magenta module
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3" CACHE PATH "OpenSSL root")
endif()
find_package(OpenSSL REQUIRED)

# IDE source groups
set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

# Create the standalone GUI app
juce_add_gui_app(SongbirdPlayer
    BUNDLE_ID "com.songbird.player"
    PRODUCT_NAME "Songbird Player"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio output"
    PLUGINHOST_AU TRUE
    PLIST_TO_MERGE "<dict><key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/></dict></dict>")

juce_generate_juce_header(SongbirdPlayer)

target_compile_features(SongbirdPlayer PRIVATE cxx_std_20)

# Source files
target_sources(SongbirdPlayer PRIVATE
    app/Main.cpp
    app/SongbirdEditor.cpp
    app/bridge/WebViewHelpers.cpp
    app/state/StateSync.cpp
    app/bridge/WebViewBridge.cpp
    app/bridge/StateBridge.cpp
    app/bridge/PluginMixerBridge.cpp
    app/bridge/BirdFileBridge.cpp
    app/bridge/TransportBridge.cpp
    app/bridge/SettingsBridge.cpp
    app/bridge/RecordingBridge.cpp
    app/bridge/TrackBridge.cpp
    app/bridge/LyriaBridge.cpp
    app/bridge/MidiEditBridge.cpp
    app/bridge/UndoRedoBridge.cpp
    app/ai/LyriaManager.cpp
    app/midi/MidiRecorder.cpp
    app/audio/AudioRecorder.cpp
    app/plugins/PluginManager.cpp
    app/audio/PlaybackInfo.cpp
    app/loader/BirdLoader.cpp
    app/plugins/MacroMapper.cpp
    app/state/ProjectState.cpp
    app/midi/MidiToBird.cpp
    app/audio/AudioQuantizer.cpp
    app/audio/LevelMeterBridge.cpp
    app/state/SessionState.cpp
    app/audio/PluginStateTracker.cpp
    app/loader/BirdFileOps.cpp
    app/export/ExportManager.cpp
    app/midi/MidiEditHelpers.cpp
    libraries/theory/note_parser.cpp
    libraries/sequencing/note.cpp
    libraries/magenta/WebSocket.cc
    libraries/magenta/Gemini.cc
    libraries/magenta/CircularBuffer.cc
    libraries/magenta/LyriaPlugin.cpp)

# Include paths
target_include_directories(SongbirdPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/websocketpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/asio/asio/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/state
    ${CMAKE_CURRENT_SOURCE_DIR}/app/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/app/midi
    ${CMAKE_CURRENT_SOURCE_DIR}/app/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/app/plugins
    ${CMAKE_CURRENT_SOURCE_DIR}/app/loader
    ${CMAKE_CURRENT_SOURCE_DIR}/app/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/app/export
    ${libgit2_SOURCE_DIR}/include
    ${libgit2_BINARY_DIR}/include)

# Compile definitions
target_compile_definitions(SongbirdPlayer PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SongbirdPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SongbirdPlayer,JUCE_VERSION>"
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_STRICT_)

# Link libraries
target_link_libraries(SongbirdPlayer PRIVATE
    tracktion::tracktion_engine
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_warning_flags
    OpenSSL::SSL
    OpenSSL::Crypto
    libgit2package)

# macOS-specific
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(SongbirdPlayer PRIVATE "-fno-aligned-allocation")
    target_link_libraries(SongbirdPlayer PRIVATE
        "-framework WebKit")
endif()

# ============================================================================
# Developer utility: scan a VST3/AU plugin and dump all parameter names
# Build:  cmake --build build --target ScanPluginParams
# Run:    ./build/ScanPluginParams_artefacts/ScanPluginParams "/Library/Audio/Plug-Ins/VST3/Console 1.vst3"
# ============================================================================
juce_add_console_app(ScanPluginParams PRODUCT_NAME "ScanPluginParams")
target_sources(ScanPluginParams PRIVATE tools/scan_plugin_params.cpp)
target_compile_features(ScanPluginParams PRIVATE cxx_std_20)
target_compile_definitions(ScanPluginParams PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="ScanPluginParams"
    JUCE_APPLICATION_VERSION_STRING="1.0")
target_link_libraries(ScanPluginParams PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_events
    juce::juce_recommended_warning_flags)
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(ScanPluginParams PRIVATE "-fno-aligned-allocation")
endif()

