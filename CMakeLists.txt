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

add_subdirectory(src)

install(TARGETS CatchChartEditor DESTINATION bin)
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
    )

    target_include_directories(CatchChartEditorTests PRIVATE src)
    target_link_libraries(CatchChartEditorTests Qt6::Core Qt6::Gui)

    add_test(NAME core_minimal_tests COMMAND CatchChartEditorTests)
    if(WIN32)
        set_tests_properties(core_minimal_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$<TARGET_FILE_DIR:Qt6::Gui>;$ENV{PATH}")
    endif()
endif()