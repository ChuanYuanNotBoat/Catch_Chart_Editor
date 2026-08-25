cmake_minimum_required(VERSION 3.16)
project(CatchChartEditor VERSION 1.11.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia LinguistTools Test)

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

add_subdirectory(src)

install(TARGETS CatchChartEditor DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/${PROJECT_NAME})
install(FILES
    third_party/QtAdvancedDockingSystem/LICENSE
    third_party/QtAdvancedDockingSystem/gnu-lgpl-v2.1.md
    DESTINATION share/${PROJECT_NAME}/licenses/QtAdvancedDockingSystem
)
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
        src/utils/CalcHash.cpp
        src/controller/ChartController.cpp
        src/file/ChartIO.cpp
        src/file/ProjectIO.cpp
        src/file/ChartFileSystem.cpp
        src/editor/NoteChain/NoteChainEditor.cpp
        src/editor/NoteChain/NoteChainState.cpp
        src/editor/NoteChain/NoteChainPersistence.cpp
    )

    target_include_directories(CatchChartEditorTests PRIVATE src)
    target_link_libraries(CatchChartEditorTests Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)

    add_test(NAME core_minimal_tests COMMAND CatchChartEditorTests)

    add_executable(DockingLayoutTests
        tests/docking_layout_tests.cpp
        src/utils/NativeWindowTheme.cpp
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

    if(WIN32)
        target_link_libraries(DockingLayoutTests PRIVATE dwmapi)
        set_tests_properties(core_minimal_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
        set_tests_properties(ui_docking_layout_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
    endif()
endif()
