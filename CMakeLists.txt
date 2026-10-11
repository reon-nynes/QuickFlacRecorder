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

file(GLOB QFR_CATALOG_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/localization/catalogs/*.json")
juce_add_binary_data(QuickFlacRecorderCatalogs HEADER_NAME TranslationResources.h NAMESPACE qfr::i18n::resources
    SOURCES src/localization/languages.json ${QFR_CATALOG_FILES})
add_library(QuickFlacRecorderTranslation STATIC src/localization/Translation.cpp src/localization/SystemLanguages.cpp)
target_include_directories(QuickFlacRecorderTranslation PUBLIC src)
target_link_libraries(QuickFlacRecorderTranslation PUBLIC juce::juce_core PRIVATE QuickFlacRecorderCatalogs)
target_compile_definitions(QuickFlacRecorderTranslation PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(APPLE)
    enable_language(OBJCXX)
    set_target_properties(QuickFlacRecorderTranslation PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
    target_sources(QuickFlacRecorderTranslation PRIVATE src/localization/SystemLanguages_mac.mm)
    target_link_libraries(QuickFlacRecorderTranslation PRIVATE "-framework Foundation")
endif()

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
target_sources(QuickFlacRecorder PRIVATE src/Main.cpp src/RecorderComponent.cpp src/RecordingPresentation.cpp)
target_link_libraries(QuickFlacRecorder PRIVATE QuickFlacRecorderInput QuickFlacRecorderTranslation juce::juce_gui_extra)
if(APPLE)
    # The OS reads this explanation before the application can render its UI.
    # Derive localized bundle resources from the same complete catalogs.
    foreach(catalog IN LISTS QFR_CATALOG_FILES)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${catalog}")
        get_filename_component(language "${catalog}" NAME_WE)
        file(READ "${catalog}" catalog_json)
        string(JSON microphone_usage GET "${catalog_json}" microphone_usage)
        string(REPLACE "\\" "\\\\" microphone_usage "${microphone_usage}")
        string(REPLACE "\"" "\\\"" microphone_usage "${microphone_usage}")
        set(usage_file "${CMAKE_CURRENT_BINARY_DIR}/localization/${language}.lproj/InfoPlist.strings")
        file(WRITE "${usage_file}" "\"NSMicrophoneUsageDescription\" = \"${microphone_usage}\";\n")
        set_source_files_properties("${usage_file}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${language}.lproj")
        target_sources(QuickFlacRecorder PRIVATE "${usage_file}")
    endforeach()
endif()

juce_add_console_app(RecorderInputCheck PRODUCT_NAME "RecorderInputCheck")
target_sources(RecorderInputCheck PRIVATE src/InputCheck.cpp)
target_link_libraries(RecorderInputCheck PRIVATE QuickFlacRecorderInput juce::juce_gui_basics)

include(CTest)
if(BUILD_TESTING)
    add_executable(RecorderTranslationTests tests/TranslationTests.cpp)
    target_link_libraries(RecorderTranslationTests PRIVATE QuickFlacRecorderTranslation)
    add_test(NAME RecorderTranslationTests COMMAND RecorderTranslationTests)
    add_executable(RecorderPresentationTests tests/RecordingPresentationTests.cpp src/RecordingPresentation.cpp)
    target_link_libraries(RecorderPresentationTests PRIVATE QuickFlacRecorderSession QuickFlacRecorderTranslation)
    add_test(NAME RecorderPresentationTests COMMAND RecorderPresentationTests)
    add_executable(RecorderUiTests tests/RecorderUiTests.cpp src/RecorderComponent.cpp src/RecordingPresentation.cpp)
    target_link_libraries(RecorderUiTests PRIVATE QuickFlacRecorderInput QuickFlacRecorderTranslation juce::juce_gui_extra)
    add_test(NAME RecorderUiTests COMMAND RecorderUiTests)
    add_executable(RecorderSessionTests tests/RecordingSessionTests.cpp)
    target_link_libraries(RecorderSessionTests PRIVATE QuickFlacRecorderSession)
    add_test(NAME RecorderSessionTests COMMAND RecorderSessionTests)
endif()
