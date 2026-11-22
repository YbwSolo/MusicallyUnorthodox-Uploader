cmake_minimum_required(VERSION 3.22)

# Version is <base>.<build number>. CI passes its run number as the build, so every release is newer than the last.
set(MUU_VERSION_BASE "0.1")
set(MUU_BUILD_NUMBER "0" CACHE STRING "Build number (CI run number); 0 for local builds")
# The Google sign-in details for YouTube. CI passes them from the repository's secrets; without them the plugin
# still builds, and the two values can be typed into Settings > Advanced instead.
set(MUU_GOOGLE_CLIENT_ID "" CACHE STRING "Google OAuth client ID (desktop app)")
set(MUU_GOOGLE_CLIENT_SECRET "" CACHE STRING "Google OAuth client secret")
project(MUUploader VERSION ${MUU_VERSION_BASE}.${MUU_BUILD_NUMBER} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

option(MUU_BUILD_TESTS "Build the test runner and the screenshot tool" ON)
option(MUU_COPY_AFTER_BUILD "Copy the built plugin into the system plugin folders" OFF)

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(MUUAssets SOURCES
    Assets/mu_logo.png
    Assets/contacts_starter.json
    Assets/fonts/Anton-Regular.ttf
    Assets/fonts/Inter-Regular.ttf
    Assets/fonts/Inter-SemiBold.ttf)
set_target_properties(MUUAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

if(UNIX AND NOT APPLE)
    set(MUU_NEEDS_CURL TRUE)     # Linux is only used for testing; there JUCE talks to the internet through libcurl
else()
    set(MUU_NEEDS_CURL FALSE)
endif()

juce_add_plugin(MUUploader
    COMPANY_NAME "Musically Unorthodox"
    COMPANY_WEBSITE "https://musicallyunorthodox.com"
    BUNDLE_ID "com.musicallyunorthodox.uploader"
    PLUGIN_MANUFACTURER_CODE Muno
    PLUGIN_CODE Muup
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "MU Uploader"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_CURL ${MUU_NEEDS_CURL}
    COPY_PLUGIN_AFTER_BUILD ${MUU_COPY_AFTER_BUILD}
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Tools
    HARDENED_RUNTIME_ENABLED TRUE)

set(MUU_CORE_SOURCES
    Source/core/Settings.cpp
    Source/core/Library.cpp
    Source/core/Capture.cpp
    Source/core/Http.cpp
    Source/core/GoogleAuth.cpp
    Source/core/Media.cpp
    Source/core/YouTube.cpp
    Source/core/Texts.cpp
    Source/core/Contacts.cpp
    Source/core/Publisher.cpp
    Source/core/Assistant.cpp
    Source/ui/Theme.cpp)

target_sources(MUUploader PRIVATE
    ${MUU_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/UploadPage.cpp
    Source/ui/BeatsPage.cpp
    Source/ui/EmailPage.cpp
    Source/ui/AssistantPage.cpp
    Source/ui/SettingsPage.cpp)

target_include_directories(MUUploader PRIVATE Source)

target_compile_definitions(MUUploader PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=$<IF:$<BOOL:${MUU_NEEDS_CURL}>,1,0>
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=1
    MUU_GOOGLE_CLIENT_ID="${MUU_GOOGLE_CLIENT_ID}"
    MUU_GOOGLE_CLIENT_SECRET="${MUU_GOOGLE_CLIENT_SECRET}")

target_link_libraries(MUUploader PRIVATE
    MUUAssets
    juce::juce_audio_utils
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(MUU_BUILD_TESTS)
    # Checks everything that does not need a DAW or an account: catching audio, the library, the words it writes,
    # the assistant, and making a real video file when ffmpeg is on the machine.
    juce_add_console_app(MUUTests PRODUCT_NAME "MUUTests")
    target_sources(MUUTests PRIVATE Source/tests/CoreTests.cpp)
    target_include_directories(MUUTests PRIVATE Source)
    target_link_libraries(MUUTests PRIVATE MUUploader MUUAssets juce::juce_audio_utils juce::juce_cryptography)
    target_compile_definitions(MUUTests PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)

    # Draws each page of the plugin to a PNG, so the look can be checked without opening a DAW.
    juce_add_gui_app(MUUPreview PRODUCT_NAME "MUUPreview")
    target_sources(MUUPreview PRIVATE Source/tests/Preview.cpp)
    target_include_directories(MUUPreview PRIVATE Source)
    target_link_libraries(MUUPreview PRIVATE MUUploader MUUAssets juce::juce_audio_utils juce::juce_cryptography)
    target_compile_definitions(MUUPreview PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
