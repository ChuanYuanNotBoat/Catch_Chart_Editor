set(SOURCES
    main.cpp
    app/Application.cpp
    app/SessionPathUtils.cpp
    app/PlaybackBenchmarkRunner.cpp
    app/MainWindow.cpp
    app/MainWindowLayout.cpp
    app/MainWindowDialogs.cpp
    app/MainWindowSkinAudio.cpp
    model/Note.cpp
    model/BpmEntry.cpp
    model/MetaData.cpp
    model/Chart.cpp
    model/Skin.cpp
    model/ChartStatistics.cpp
    utils/MathUtils.cpp
    utils/FileUtils.cpp
    utils/Settings.cpp
    utils/NativeWindowTheme.cpp
    utils/Logger.cpp
    utils/Translator.cpp
    utils/PerformanceTimer.cpp
    utils/PlaybackStutterProbe.cpp
    utils/DiagnosticCollector.cpp
    utils/CalcHash.cpp
    file/ChartIO.cpp
    file/SkinIO.cpp
    file/ProjectIO.cpp
    file/PluginLoader.cpp
    file/ChartFileSystem.cpp
    audio/AudioPlayer.cpp
    audio/NoteSoundPlayer.cpp
    audio/BpmDetector.cpp
    audio/AutoTiming2Bridge.cpp
    audio/AudioConverter.cpp
    controller/ChartController.cpp
    controller/SelectionController.cpp
    controller/DisplayFrameScheduler.cpp
    controller/PlaybackController.cpp
    render/NoteRenderer.cpp
    render/RainRewardGenerator.cpp
    render/BeatDivisionColor.cpp
    render/GridRenderer.cpp
    render/BackgroundRenderer.cpp
    render/HyperfruitDetector.cpp
    plugin/ExternalProcessPlugin.cpp
    plugin/PluginManager.cpp
    ui/CustomWidgets/RightPanel.cpp
    ui/CustomWidgets/RealtimePreviewWidget.cpp
    ui/LongRangeSelector.cpp
    ui/PlaybackSpeedPanel.cpp
    ui/DockLayoutPolicy.cpp
    ui/PaneContainer.cpp
    ui/WorkbenchLayout.cpp
    ui/ChartStatsPanel.cpp
    ui/PluginActionPanel.cpp
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
    ui/CustomWidgets/ChartCanvas/ChartCanvasNoteChain.cpp
    ui/dialogs/LogSettingsDialog.cpp
    ui/dialogs/PluginManagerDialog.cpp
    ui/dialogs/BpmMeasureDialog.cpp
    ui/dialogs/DetailedStatsDialog.cpp
    editor/NoteChain/NoteChainEditor.cpp
    editor/NoteChain/NoteChainState.cpp
    editor/NoteChain/NoteChainPersistence.cpp
)

# ---- Vendored AutoTimingCore (upstream=90f7a95, see docs/AUTOTIMING_VENDORING.md) ----
# Malody legacy AutoTiming baseline, kept as a static library with the CCE host
# adaptations (Result naming, k* constants, full FMOD format enum).
set(AUTOTIMING_LEGACY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/audio/autotiming)
add_library(autotiming_legacy STATIC
    ${AUTOTIMING_LEGACY_DIR}/AutoTiming.cpp
    ${AUTOTIMING_LEGACY_DIR}/AutoTiming.h
    ${AUTOTIMING_LEGACY_DIR}/dsp.cpp
    ${AUTOTIMING_LEGACY_DIR}/dsp.h
    ${AUTOTIMING_LEGACY_DIR}/fft.cpp
    ${AUTOTIMING_LEGACY_DIR}/fft.h
    ${AUTOTIMING_LEGACY_DIR}/platform.h
    ${AUTOTIMING_LEGACY_DIR}/util.cpp
    ${AUTOTIMING_LEGACY_DIR}/util.h
)
target_include_directories(autotiming_legacy PUBLIC
    "$<BUILD_INTERFACE:${AUTOTIMING_LEGACY_DIR}>"
)
target_compile_features(autotiming_legacy PUBLIC cxx_std_17)
if(MSVC)
    # /EHsc: vendored legacy code and the analysis core use exceptions for
    # error/abstain signalling; the host project omits /EHsc by default.
    target_compile_options(autotiming_legacy PRIVATE /W3 /permissive- /utf-8 /EHsc)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
    target_compile_options(autotiming_legacy PRIVATE -msse2 -mfpmath=sse)
endif()

# AutoTiming 2 analysis pipeline (autotiming::analyze), vendored verbatim from
# upstream AutoTimingCore@90f7a95 under src/audio/autotiming/core/.
set(AUTOTIMING_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/audio/autotiming/core)
add_library(autotiming_core STATIC
    ${AUTOTIMING_CORE_DIR}/include/autotiming/Analysis.h
    ${AUTOTIMING_CORE_DIR}/src/Analysis.cpp
    ${AUTOTIMING_CORE_DIR}/src/Periodicity.cpp
    ${AUTOTIMING_CORE_DIR}/src/Periodicity.h
    ${AUTOTIMING_CORE_DIR}/src/RhythmLayers.cpp
    ${AUTOTIMING_CORE_DIR}/src/RhythmLayers.h
    ${AUTOTIMING_CORE_DIR}/src/TempoTracker.cpp
    ${AUTOTIMING_CORE_DIR}/src/TempoTracker.h
)
target_include_directories(autotiming_core PUBLIC
    "$<BUILD_INTERFACE:${AUTOTIMING_CORE_DIR}/include>"
)
target_link_libraries(autotiming_core PRIVATE autotiming_legacy)
target_compile_features(autotiming_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(autotiming_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
    target_compile_options(autotiming_core PRIVATE -msse2 -mfpmath=sse)
endif()

qt_add_executable(CatchChartEditor
    MANUAL_FINALIZATION
    ${SOURCES}
)

target_link_libraries(CatchChartEditor
    PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Concurrent
    ads::qtadvanceddocking-qt6
    Vorbis::vorbis
    autotiming_core
)

if(WIN32)
    target_link_libraries(CatchChartEditor PRIVATE dwmapi)
endif()

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

# ---- Auto-update translation source files ----
# Run: cmake --build . --target CatchChartEditor_lupdate
# This scans source code for tr() calls and updates .ts files.
qt_add_lupdate(CatchChartEditor
    TS_FILES ${TRANSLATION_TS_FILES}
    SOURCES ${SOURCES}
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
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/QtAdvancedDockingSystem
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/QtAdvancedDockingSystem/LICENSE
        ${CMAKE_SOURCE_DIR}/third_party/QtAdvancedDockingSystem/gnu-lgpl-v2.1.md
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/QtAdvancedDockingSystem
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/libogg
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/libvorbis
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/libogg/COPYING
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/libogg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/libvorbis/COPYING
        $<TARGET_FILE_DIR:CatchChartEditor>/licenses/libvorbis
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
