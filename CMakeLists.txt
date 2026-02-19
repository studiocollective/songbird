cmake_minimum_required(VERSION 3.15)

project(SongbirdPlayer VERSION 1.0.0)

# macOS deployment target
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE INTERNAL "")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE INTERNAL "")
endif()

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
    app/BirdLoader.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR})

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
    OpenSSL::Crypto)

# macOS-specific
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(SongbirdPlayer PRIVATE "-fno-aligned-allocation")
    target_link_libraries(SongbirdPlayer PRIVATE
        "-framework WebKit")
endif()
