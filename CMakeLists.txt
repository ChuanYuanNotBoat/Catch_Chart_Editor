cmake_minimum_required(VERSION 3.16)
project(CatchChartEditor VERSION 1.10.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia LinguistTools)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

set(SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/MainWindow.cpp
    src/app/MainWindowDialogs.cpp
    src/app/MainWindowSkinAudio.cpp
    src/model/Note.cpp
    src/model/BpmEntry.cpp
    src/model/MetaData.cpp
    src/model/Chart.cpp
    src/model/Skin.cpp
    src/utils/MathUtils.cpp
    src/utils/FileUtils.cpp
    src/utils/Settings.cpp
    src/utils/Logger.cpp
    src/utils/Translator.cpp
    src/utils/PerformanceTimer.cpp
    src/utils/PlaybackStutterProbe.cpp
    src/utils/DiagnosticCollector.cpp
    src/file/ChartIO.cpp
    src/file/SkinIO.cpp
    src/file/ProjectIO.cpp
    src/file/BpmAuxFiles.cpp
    src/file/ChartFileSystem.cpp
    src/file/PluginLoader.cpp
    src/audio/AudioPlayer.cpp
    src/audio/BpmDetector.cpp
    src/audio/autotiming/AutoTiming.cpp
    src/audio/autotiming/dsp.cpp
    src/audio/autotiming/fft.cpp
    src/audio/autotiming/util.cpp
    src/audio/NoteSoundPlayer.cpp
    src/controller/ChartController.cpp
    src/controller/SelectionController.cpp
    src/controller/PlaybackController.cpp
    src/render/NoteRenderer.cpp
    src/render/BeatDivisionColor.cpp
    src/render/GridRenderer.cpp
    src/render/BackgroundRenderer.cpp
    src/render/HyperfruitDetector.cpp
    src/plugin/ExternalProcessPlugin.cpp
    src/plugin/PluginManager.cpp
    src/ui/CustomWidgets/RightPanel.cpp
    src/ui/CustomWidgets/RealtimePreviewWidget.cpp
    src/ui/NoteEditPanel.cpp
    src/ui/BPMTimePanel.cpp
    src/ui/MetaEditPanel.cpp
    src/ui/SpeedPopup.cpp
    src/ui/LeftPanel.cpp
    src/ui/TimelineWidget.cpp
    src/ui/DensityCurve.cpp
    src/ui/NoteItem.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvas.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvasPlayback.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvasPaste.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvasRender.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvasInteraction.cpp
    src/ui/CustomWidgets/ChartCanvas/ChartCanvasMouse.cpp
    src/ui/dialogs/LogSettingsDialog.cpp
    src/ui/dialogs/PluginManagerDialog.cpp
    src/ui/dialogs/BpmMeasureDialog.cpp
)

qt_add_executable(${PROJECT_NAME}
    MANUAL_FINALIZATION
    ${SOURCES}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
)
if(COMMAND qt_finalize_executable)
    qt_finalize_executable(${PROJECT_NAME})
endif()

set(_default_enable_windeployqt ON)
if(DEFINED ENV{CI} AND "$ENV{CI}" STREQUAL "true")
    set(_default_enable_windeployqt OFF)
endif()
option(CATCHEDITOR_ENABLE_WINDEPLOYQT "Deploy Qt runtime with windeployqt after build (Windows only)." ${_default_enable_windeployqt})

if(WIN32 AND CATCHEDITOR_ENABLE_WINDEPLOYQT)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    if(QT_QMAKE_EXECUTABLE)
        get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        set(WINDEPLOYQT_EXECUTABLE "${QT_BIN_DIR}/windeployqt.exe")
        if(EXISTS "${WINDEPLOYQT_EXECUTABLE}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:${PROJECT_NAME}>"
                COMMENT "Deploying Qt runtime dependencies with windeployqt"
            )
        endif()
    endif()
endif()

# Add `src` include dir so project headers can be included as "model/Note.h".
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Translation files.
set(TRANSLATION_TS_FILES
    resources/translations/catch_editor_en_US.ts
    resources/translations/catch_editor_zh_CN.ts
    resources/translations/catch_editor_ja_JP.ts
)

qt6_add_translations(${PROJECT_NAME}
    RESOURCE_PREFIX /translations
    TS_FILES ${TRANSLATION_TS_FILES}
)

# Copy default skin assets after build.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/default_skin
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/skins/default
)

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/note_sounds")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/note_sounds
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/note_sounds
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/plugins")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/plugins
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/docs
    )
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/${PROJECT_NAME})
if(EXISTS "${CMAKE_SOURCE_DIR}/plugins")
    install(DIRECTORY plugins/ DESTINATION bin/plugins)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(CatchChartEditorTests
        tests/minimal_tests.cpp
        src/model/Note.cpp
        src/model/BpmEntry.cpp
        src/model/MetaData.cpp
        src/model/Chart.cpp
        src/utils/MathUtils.cpp
        src/utils/Logger.cpp
        src/utils/PerformanceTimer.cpp
        src/utils/DiagnosticCollector.cpp
        src/controller/ChartController.cpp
        src/file/ChartIO.cpp
        src/file/ProjectIO.cpp
        src/file/BpmAuxFiles.cpp
        src/file/ChartFileSystem.cpp
    )

    target_include_directories(CatchChartEditorTests PRIVATE src)
    target_link_libraries(CatchChartEditorTests Qt6::Core Qt6::Gui)

    add_test(NAME core_minimal_tests COMMAND CatchChartEditorTests)
    if(WIN32)
        set_tests_properties(core_minimal_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
    endif()
endif()
