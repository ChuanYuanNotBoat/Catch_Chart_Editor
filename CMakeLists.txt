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
    third_party/AutoTimingCore/ATTRIBUTION.md
    DESTINATION share/${PROJECT_NAME}/notices/AutoTimingCore
)
if(EXISTS "${CMAKE_SOURCE_DIR}/plugins")
    install(DIRECTORY plugins/ DESTINATION bin/plugins)
endif()

if(BUILD_TESTING)
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
