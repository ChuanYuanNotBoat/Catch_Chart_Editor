cmake_minimum_required(VERSION 3.20)
project(CatchChartEditor VERSION 1.11.2 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia LinguistTools Test Concurrent)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# Qt Advanced Docking System 5.1.1 is vendored so release builds do not
# depend on network access. Keep the dependency static and omit its examples.
set(BUILD_STATIC ON)
set(BUILD_EXAMPLES OFF)
add_subdirectory(third_party/QtAdvancedDockingSystem EXCLUDE_FROM_ALL)
unset(BUILD_STATIC)
unset(BUILD_EXAMPLES)

# Vendored Xiph.Org codecs (BSD-style licenses, see third_party/README.md)
# used by AudioConverter to encode non-OGG chart music to OGG Vorbis.
add_subdirectory(third_party/libogg EXCLUDE_FROM_ALL)
add_subdirectory(third_party/libvorbis EXCLUDE_FROM_ALL)

# AutoTimingCore is a pinned git submodule in production. An explicit cache
# override lets maintainers validate an adjacent upstream worktree before its
# reviewed commit is pinned; release/CI builds keep the submodule default.
set(AUTOTIMINGCORE_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/third_party/AutoTimingCore"
    CACHE PATH "AutoTimingCore source directory (development override)")
if(NOT EXISTS "${AUTOTIMINGCORE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "AutoTimingCore source is unavailable at ${AUTOTIMINGCORE_SOURCE_DIR}. "
        "Initialize the default submodule with: git submodule update --init --recursive")
endif()
if(NOT DEFINED AUTOTIMING_BUILD_TOOLS)
    set(AUTOTIMING_BUILD_TOOLS ${BUILD_TESTING})
endif()
add_subdirectory("${AUTOTIMINGCORE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/AutoTimingCore")

# Workaround for MSVC Release optimizer crash on specific files.
# Disable optimization for ChartCanvas.cpp and ChartCanvasPlayback.cpp.
if(MSVC)
    set_source_files_properties(
        src/ui/CustomWidgets/ChartCanvas/ChartCanvas.cpp
        src/ui/CustomWidgets/ChartCanvas/ChartCanvasPlayback.cpp
        PROPERTIES COMPILE_OPTIONS "/Od"
    )
endif()

add_subdirectory(src)

option(CATCHEDITOR_BUILD_AUDIO_PROBE "Build the manual Windows note-sound loopback probe" OFF)
if(CATCHEDITOR_BUILD_AUDIO_PROBE AND WIN32)
    if(Qt6_VERSION VERSION_LESS 6.10)
        message(FATAL_ERROR "The manual audio probe requires Qt 6.10 or newer")
    endif()
    add_executable(NoteSoundTimingProbe tools/perf/note_sound_timing_probe.cpp)
    target_link_libraries(NoteSoundTimingProbe PRIVATE CatchChartEditorRuntime ole32 uuid)
endif()

install(TARGETS CatchChartEditor DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/${PROJECT_NAME})
install(FILES
    third_party/QtAdvancedDockingSystem/LICENSE
    third_party/QtAdvancedDockingSystem/gnu-lgpl-v2.1.md
    DESTINATION share/${PROJECT_NAME}/licenses/QtAdvancedDockingSystem
)
install(FILES
    third_party/libogg/COPYING
    DESTINATION share/${PROJECT_NAME}/licenses/libogg
)
install(FILES
    third_party/libvorbis/COPYING
    DESTINATION share/${PROJECT_NAME}/licenses/libvorbis
)
install(FILES
    "${AUTOTIMINGCORE_SOURCE_DIR}/ATTRIBUTION.md"
    DESTINATION share/${PROJECT_NAME}/notices/AutoTimingCore
)
if(EXISTS "${CMAKE_SOURCE_DIR}/plugins")
    install(DIRECTORY plugins/ DESTINATION bin/plugins)
endif()

if(BUILD_TESTING)
    function(add_local_review_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE CatchChartEditorRuntime Qt6::Test)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
        add_test(NAME ${target} COMMAND ${target})
        get_target_property(_review_qmake Qt6::qmake IMPORTED_LOCATION)
        execute_process(COMMAND "${_review_qmake}" -query QT_INSTALL_PLUGINS
            OUTPUT_VARIABLE _review_plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
        set_tests_properties(${target} PROPERTIES TIMEOUT 180 ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${_review_plugins}/platforms")
        if(WIN32)
            string(REPLACE ";" "\\;" _review_test_path "$ENV{PATH}")
            set_tests_properties(${target} PROPERTIES ENVIRONMENT
                "QT_QPA_PLATFORM=minimal;QT_QPA_FONTDIR=C:/Windows/Fonts;QT_QPA_PLATFORM_PLUGIN_PATH=${_review_plugins}/platforms;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_review_test_path}")
        endif()
    endfunction()
    add_local_review_test(LocalIdentityTests tests/local_identity_tests.cpp)
    add_local_review_test(DocumentModelTests tests/document_model_tests.cpp)
    add_local_review_test(ProjectStorageTests tests/project_storage_tests.cpp)
    add_local_review_test(ReviewCoreTests tests/review_core_tests.cpp)
    add_local_review_test(CommentModelTests tests/comment_model_tests.cpp)
    add_local_review_test(ReviewInspectorTests tests/review_inspector_tests.cpp)
    add_local_review_test(ExportPipelineTests tests/export_pipeline_tests.cpp)
    add_local_review_test(ReviewWorkspaceTests tests/review_workspace_tests.cpp)
    add_executable(GridDragTests tests/grid_drag_tests.cpp)
    target_link_libraries(GridDragTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    add_test(NAME ui_grid_drag_tests COMMAND GridDragTests)
    set_tests_properties(ui_grid_drag_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        string(REPLACE ";" "\\;" _grid_drag_test_path "$ENV{PATH}")
        set_tests_properties(ui_grid_drag_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_grid_drag_test_path}")
    endif()

    add_executable(ResourceCopyTests tests/resource_copy_tests.cpp)
    target_link_libraries(ResourceCopyTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    if(MSVC)
        target_compile_options(ResourceCopyTests PRIVATE /utf-8)
    endif()
    add_test(NAME file_resource_copy_tests COMMAND ResourceCopyTests)
    set_tests_properties(file_resource_copy_tests PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        string(REPLACE ";" "\\;" _resource_copy_test_path "$ENV{PATH}")
        set_tests_properties(file_resource_copy_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_resource_copy_test_path}")
    endif()

    add_executable(BpmDetectorFileTests tests/bpm_detector_file_tests.cpp)
    target_link_libraries(BpmDetectorFileTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    if(MSVC)
        target_compile_options(BpmDetectorFileTests PRIVATE /utf-8)
    endif()
    add_test(NAME audio_bpm_detector_file_tests COMMAND BpmDetectorFileTests)
    set_tests_properties(audio_bpm_detector_file_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        string(REPLACE ";" "\\;" _bpm_file_test_path "$ENV{PATH}")
        set_tests_properties(audio_bpm_detector_file_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_bpm_file_test_path}")
    endif()

    add_executable(PlaybackTimingTests tests/playback_timing_tests.cpp)
    target_link_libraries(PlaybackTimingTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    add_test(NAME audio_playback_timing_tests COMMAND PlaybackTimingTests)
    set_tests_properties(audio_playback_timing_tests PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        string(REPLACE ";" "\\;" _playback_test_path "$ENV{PATH}")
        set_tests_properties(audio_playback_timing_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_playback_test_path}")
    endif()

    add_executable(TimingInterpolationTests tests/timing_interpolation_tests.cpp src/analysis/TimingInterpolation.cpp)
    set_target_properties(TimingInterpolationTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(TimingInterpolationTests PRIVATE src)
    add_test(NAME analysis_timing_interpolation_tests COMMAND TimingInterpolationTests)

    add_executable(TimingMeasurementTests tests/timing_measurement_tests.cpp src/analysis/TimingMeasurement.cpp)
    set_target_properties(TimingMeasurementTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(TimingMeasurementTests PRIVATE src)
    add_test(NAME analysis_timing_measurement_tests COMMAND TimingMeasurementTests)

    add_executable(SpectrumAnalysisTests tests/spectrum_analysis_tests.cpp)
    target_link_libraries(SpectrumAnalysisTests PRIVATE CatchSpectrumDsp)
    set_target_properties(SpectrumAnalysisTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(SpectrumAnalysisTests PRIVATE src)
    add_test(NAME analysis_spectrum_tests COMMAND SpectrumAnalysisTests)

    add_executable(SpectrumOverlayTests tests/spectrum_overlay_tests.cpp)
    target_link_libraries(SpectrumOverlayTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    add_test(NAME ui_spectrum_overlay_tests COMMAND SpectrumOverlayTests)
    set_tests_properties(ui_spectrum_overlay_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        # The Windows offscreen font backend can crash on consecutive bare
        # ChartCanvas windows. Minimal retains pixel and event-loop checks.
        string(REPLACE ";" "\\;" _spectrum_test_path "$ENV{PATH}")
        set_tests_properties(ui_spectrum_overlay_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=minimal;PATH=$<TARGET_FILE_DIR:Qt6::Core>\;${_spectrum_test_path}")
        if(TARGET Qt6::QMinimalIntegrationPlugin)
            add_custom_command(TARGET SpectrumOverlayTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SpectrumOverlayTests>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:SpectrumOverlayTests>/platforms"
                COMMENT "Deploying the spectrum test platform plugin"
            )
        endif()
    endif()

    add_executable(TransientAnalysisTests tests/transient_analysis_tests.cpp src/analysis/TransientAnalysis.cpp)
    target_link_libraries(TransientAnalysisTests PRIVATE CatchSpectrumDsp)
    set_target_properties(TransientAnalysisTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(TransientAnalysisTests PRIVATE src)
    add_test(NAME analysis_transient_tests COMMAND TransientAnalysisTests)

    add_executable(AnalysisEditorTests tests/analysis_editor_tests.cpp)
    target_link_libraries(AnalysisEditorTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    add_test(NAME analysis_editor_tests COMMAND AnalysisEditorTests)
    set_tests_properties(analysis_editor_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        get_target_property(_ae_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_ae_qt_bin "${_ae_qmake}" DIRECTORY)
        if(CATCHEDITOR_ENABLE_WINDEPLOYQT AND EXISTS "${_ae_qt_bin}/windeployqt.exe")
            add_custom_command(TARGET AnalysisEditorTests POST_BUILD
                COMMAND "${_ae_qt_bin}/windeployqt.exe" --no-translations "$<TARGET_FILE:AnalysisEditorTests>"
                COMMENT "Deploying the AE test runtime"
            )
        endif()
        add_custom_command(TARGET AnalysisEditorTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:AnalysisEditorTests>"
        )
        if(TARGET Qt6::QOffscreenIntegrationPlugin)
            add_custom_command(TARGET AnalysisEditorTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnalysisEditorTests>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:AnalysisEditorTests>/platforms"
            )
            set_tests_properties(analysis_editor_tests PROPERTIES ENVIRONMENT
                "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:AnalysisEditorTests>/platforms"
            )
        endif()
    endif()

    add_executable(ShortcutTests tests/shortcut_tests.cpp)
    target_link_libraries(ShortcutTests PRIVATE CatchChartEditorRuntime Qt6::Test)
    add_test(NAME ui_shortcut_tests COMMAND ShortcutTests)
    set_tests_properties(ui_shortcut_tests PROPERTIES TIMEOUT 60)
    if(WIN32)
        set_tests_properties(ui_shortcut_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
    endif()

    add_executable(CatchChartEditorTests
        tests/minimal_tests.cpp
        src/app/SessionPathUtils.cpp
        src/model/Note.cpp
        src/model/BpmEntry.cpp
        src/model/MetaData.cpp
        src/model/Chart.cpp
        src/utils/MathUtils.cpp
        src/utils/Logger.cpp
        src/utils/Settings.cpp
        src/utils/PerformanceTimer.cpp
        src/utils/DiagnosticCollector.cpp
        src/utils/CalcHash.cpp
        src/utils/FileUtils.cpp
        src/utils/ImageConverter.cpp
        src/controller/ChartController.cpp
        src/controller/SelectionController.cpp
        src/render/RainRewardGenerator.cpp
        src/model/ChartStatistics.cpp
        src/file/ChartIO.cpp
        src/file/ProjectIO.cpp
        src/file/ChartFileSystem.cpp
        src/editor/NoteChain/NoteChainEditor.cpp
        src/editor/NoteChain/NoteChainState.cpp
        src/editor/NoteChain/NoteChainPersistence.cpp
        src/audio/AudioConverter.cpp
        src/plugin/ExternalProcessPlugin.cpp
    )

    target_include_directories(CatchChartEditorTests PRIVATE src)
    target_link_libraries(CatchChartEditorTests
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::Concurrent Vorbis::vorbis)

    add_test(NAME core_minimal_tests COMMAND CatchChartEditorTests)
    set_tests_properties(core_minimal_tests PROPERTIES TIMEOUT 60)

    add_executable(DockingLayoutTests
        tests/docking_layout_tests.cpp
        src/utils/NativeWindowTheme.cpp
        src/utils/Settings.cpp
        src/ui/DockLayoutPolicy.cpp
        src/ui/PaneContainer.cpp
        src/ui/WorkbenchLayout.cpp
        src/ui/PlaybackSpeedPanel.cpp
    )
    target_include_directories(DockingLayoutTests PRIVATE src)
    target_link_libraries(DockingLayoutTests
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        ads::qtadvanceddocking-qt6
    )
    add_test(NAME ui_docking_layout_tests COMMAND DockingLayoutTests)

    add_executable(SettingsTransferTests
        tests/settings_transfer_tests.cpp
        src/utils/Settings.cpp
    )
    target_include_directories(SettingsTransferTests PRIVATE src)
    target_link_libraries(SettingsTransferTests PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME utils_settings_transfer_tests COMMAND SettingsTransferTests)

    add_executable(MetaEditPanelTests
        tests/meta_edit_panel_tests.cpp
        src/model/Note.cpp
        src/model/BpmEntry.cpp
        src/model/MetaData.cpp
        src/model/Chart.cpp
        src/utils/Logger.cpp
        src/utils/PerformanceTimer.cpp
        src/utils/DiagnosticCollector.cpp
        src/utils/CalcHash.cpp
        src/utils/FileUtils.cpp
        src/file/ChartIO.cpp
        src/controller/ChartController.cpp
        src/audio/AudioConverter.cpp
        src/utils/ImageConverter.cpp
        src/ui/CustomWidgets/RightPanel.cpp
        src/ui/MetaEditPanel.cpp
    )
    target_include_directories(MetaEditPanelTests PRIVATE src)
    target_link_libraries(MetaEditPanelTests
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        Qt6::Multimedia
        Vorbis::vorbis
    )
    add_test(NAME ui_meta_edit_panel_tests COMMAND MetaEditPanelTests)

    add_executable(ImageConverterTests
        tests/image_converter_tests.cpp
        src/utils/ImageConverter.cpp
    )
    target_include_directories(ImageConverterTests PRIVATE src)
    target_link_libraries(ImageConverterTests
        PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )
    add_test(NAME utils_image_converter_tests COMMAND ImageConverterTests)
    if(WIN32)
        set_tests_properties(utils_image_converter_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()

    add_executable(BpmMeasurementTests
        tests/bpm_measurement_tests.cpp
        src/audio/AutoTiming2Bridge.cpp
        src/model/Note.cpp
        src/model/BpmEntry.cpp
        src/utils/MathUtils.cpp
        src/utils/Logger.cpp
        src/utils/Settings.cpp
        src/utils/NativeWindowTheme.cpp
        src/ui/BpmMeasureUtils.cpp
        src/ui/dialogs/BpmMeasureDialog.cpp
    )
    target_include_directories(BpmMeasurementTests PRIVATE src)
    target_link_libraries(BpmMeasurementTests
        PRIVATE
        AutoTimingCore::core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(BpmMeasurementTests PRIVATE /utf-8 /EHsc)
    endif()
    if(WIN32)
        target_link_libraries(BpmMeasurementTests PRIVATE dwmapi)
    endif()
    add_test(NAME ui_bpm_measurement_tests COMMAND BpmMeasurementTests)

    # ---- CCE AutoTiming 2 bridge tests (synthetic pulse trains, no UI) ----
    add_executable(AutoTimingBridgeTests
        tests/autotiming_bridge_tests.cpp
        src/audio/AutoTiming2Bridge.cpp
        src/audio/BpmDetector.cpp
    )
    target_include_directories(AutoTimingBridgeTests PRIVATE src)
    target_link_libraries(AutoTimingBridgeTests
        PRIVATE
        Qt6::Core
        Qt6::Multimedia
        AutoTimingCore::core
        AutoTimingCore::legacy
    )
    if(MSVC)
        # /utf-8: shared TUs (BpmDetector.cpp / AutoTiming2Bridge.cpp) carry
        # UTF-8 Chinese literals and are also built with /utf-8 in the app;
        # the test cpp itself contains the same literals. /EHsc: the bridge
        # catches exceptions thrown by the vendored analysis core.
        target_compile_options(AutoTimingBridgeTests PRIVATE /utf-8 /EHsc)
    endif()
    add_test(NAME audio_autotiming_bridge_tests COMMAND AutoTimingBridgeTests)
    if(WIN32)
        set_tests_properties(audio_autotiming_bridge_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()

    if(WIN32)
        target_link_libraries(DockingLayoutTests PRIVATE dwmapi)
        set_tests_properties(core_minimal_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
        set_tests_properties(ui_docking_layout_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
        set_tests_properties(utils_settings_transfer_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
        set_tests_properties(ui_meta_edit_panel_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
        set_tests_properties(ui_bpm_measurement_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
    endif()
endif()
