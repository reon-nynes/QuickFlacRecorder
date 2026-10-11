cmake_minimum_required(VERSION 3.22)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(QuickFlacRecorder VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
find_package(Threads REQUIRED)

add_library(QuickFlacRecorderSession STATIC src/RecordingSession.cpp)
target_include_directories(QuickFlacRecorderSession PUBLIC src)
target_link_libraries(QuickFlacRecorderSession PUBLIC juce::juce_audio_formats PRIVATE Threads::Threads)
target_compile_definitions(QuickFlacRecorderSession PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1)

add_library(QuickFlacRecorderInput STATIC src/InputController.cpp)
target_link_libraries(QuickFlacRecorderInput PUBLIC QuickFlacRecorderSession juce::juce_audio_devices)
target_compile_definitions(QuickFlacRecorderInput PUBLIC
    JUCE_ASIO=$<BOOL:$<PLATFORM_ID:Windows>> JUCE_DIRECTSOUND=0 JUCE_MODAL_LOOPS_PERMITTED=1)
set(QFR_JACK_ENABLED 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(QFR_JACK QUIET jack)
        if(QFR_JACK_FOUND)
            set(QFR_JACK_ENABLED 1)
            target_include_directories(QuickFlacRecorderInput PUBLIC ${QFR_JACK_INCLUDE_DIRS})
        endif()
    endif()
endif()
target_compile_definitions(QuickFlacRecorderInput PUBLIC JUCE_JACK=${QFR_JACK_ENABLED})

juce_add_gui_app(QuickFlacRecorder
    PRODUCT_NAME "QuickFlacRecorder"
    COMPANY_NAME "reon-nynes"
    BUNDLE_ID "com.reon-nynes.quickflacrecorder"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "QuickFlacRecorder uses your selected microphone or audio interface to record FLAC files.")
target_sources(QuickFlacRecorder PRIVATE src/Main.cpp src/RecorderComponent.cpp)
target_link_libraries(QuickFlacRecorder PRIVATE QuickFlacRecorderInput juce::juce_gui_extra)

juce_add_console_app(RecorderInputCheck PRODUCT_NAME "RecorderInputCheck")
target_sources(RecorderInputCheck PRIVATE src/InputCheck.cpp)
target_link_libraries(RecorderInputCheck PRIVATE QuickFlacRecorderInput juce::juce_gui_basics)

include(CTest)
if(BUILD_TESTING)
    add_executable(RecorderSessionTests tests/RecordingSessionTests.cpp)
    target_link_libraries(RecorderSessionTests PRIVATE QuickFlacRecorderSession)
    add_test(NAME RecorderSessionTests COMMAND RecorderSessionTests)
endif()
