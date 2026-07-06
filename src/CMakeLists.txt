set(SOURCES
    main.cpp
    app/Application.cpp
    app/MainWindow.cpp
    app/MainWindowDialogs.cpp
    app/MainWindowSkinAudio.cpp
    model/Note.cpp
    model/BpmEntry.cpp
    model/MetaData.cpp
    model/Chart.cpp
    model/Skin.cpp
    utils/MathUtils.cpp
    utils/FileUtils.cpp
    utils/Settings.cpp
    utils/Logger.cpp
    utils/Translator.cpp
    utils/PerformanceTimer.cpp
    utils/PlaybackStutterProbe.cpp
    utils/DiagnosticCollector.cpp
    file/ChartIO.cpp
    file/SkinIO.cpp
    file/ProjectIO.cpp
    file/PluginLoader.cpp
    file/ChartFileSystem.cpp
    audio/AudioPlayer.cpp
    audio/NoteSoundPlayer.cpp
    audio/BpmDetector.cpp
    audio/autotiming/AutoTiming.cpp
    audio/autotiming/dsp.cpp
    audio/autotiming/fft.cpp
    audio/autotiming/util.cpp
    controller/ChartController.cpp
    controller/SelectionController.cpp
    controller/PlaybackController.cpp
    render/NoteRenderer.cpp
    render/BeatDivisionColor.cpp
    render/GridRenderer.cpp
    render/BackgroundRenderer.cpp
    render/HyperfruitDetector.cpp
    plugin/ExternalProcessPlugin.cpp
    plugin/PluginManager.cpp
    ui/CustomWidgets/RightPanel.cpp
    ui/CustomWidgets/RealtimePreviewWidget.cpp
    ui/NoteEditPanel.cpp
    ui/BPMTimePanel.cpp
    ui/MetaEditPanel.cpp
    ui/SpeedPopup.cpp
    ui/LeftPanel.cpp
    ui/TimelineWidget.cpp
    ui/DensityCurve.cpp
    ui/NoteItem.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvas.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvasPlayback.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvasPaste.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvasRender.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvasInteraction.cpp
    ui/CustomWidgets/ChartCanvas/ChartCanvasMouse.cpp
    ui/dialogs/LogSettingsDialog.cpp
    ui/dialogs/PluginManagerDialog.cpp
    ui/dialogs/BpmMeasureDialog.cpp
    editor/NoteChain/NoteChainEditor.cpp
    editor/NoteChain/NoteChainState.cpp
    editor/NoteChain/NoteChainHistory.cpp
    editor/NoteChain/NoteChainPersistence.cpp
    editor/NoteChain/NoteChainCurveSampler.cpp
    editor/NoteChain/NoteChainOverlay.cpp
)

qt_add_executable(CatchChartEditor
    MANUAL_FINALIZATION
    ${SOURCES}
)

target_link_libraries(CatchChartEditor
    PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
)

if(COMMAND qt_finalize_executable)
    qt_finalize_executable(CatchChartEditor)
endif()

target_include_directories(CatchChartEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Translation files.
set(TRANSLATION_TS_FILES
    ${CMAKE_SOURCE_DIR}/resources/translations/catch_editor_en_US.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/catch_editor_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/resources/translations/catch_editor_ja_JP.ts
)

qt6_add_translations(CatchChartEditor
    RESOURCE_PREFIX /translations
    TS_FILES ${TRANSLATION_TS_FILES}
)

# ---- windeployqt ----
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
            add_custom_command(TARGET CatchChartEditor POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:CatchChartEditor>"
                COMMENT "Deploying Qt runtime dependencies with windeployqt"
            )
        endif()
    endif()
endif()

# ---- Copy resources after build ----
add_custom_command(TARGET CatchChartEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/default_skin
        $<TARGET_FILE_DIR:CatchChartEditor>/skins/default
)

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/note_sounds")
    add_custom_command(TARGET CatchChartEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/note_sounds
            $<TARGET_FILE_DIR:CatchChartEditor>/resources/note_sounds
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/plugins")
    add_custom_command(TARGET CatchChartEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/plugins
            $<TARGET_FILE_DIR:CatchChartEditor>/plugins
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
    add_custom_command(TARGET CatchChartEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/docs
            $<TARGET_FILE_DIR:CatchChartEditor>/docs
    )
endif()