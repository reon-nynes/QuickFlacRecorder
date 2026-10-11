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

include(CTest)
if(BUILD_TESTING)
    add_executable(RecorderSessionTests tests/RecordingSessionTests.cpp)
    target_link_libraries(RecorderSessionTests PRIVATE QuickFlacRecorderSession)
    add_test(NAME RecorderSessionTests COMMAND RecorderSessionTests)
endif()
