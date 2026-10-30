# This package uses only Phobos: compile directly with LDC, without downloading
# dependencies or writing generated executables into the vendored source tree.
find_program(CATCHEDITOR_LDC_COMPILER NAMES ldc2 REQUIRED)

set(_ctb_binary "${CMAKE_CURRENT_BINARY_DIR}/malodycatch2ctb-d${CMAKE_EXECUTABLE_SUFFIX}")
set(_ctb_manifest "${CMAKE_CURRENT_BINARY_DIR}/malodycatch2ctb-d.plugin.json")
if(WIN32)
    configure_file(plugin/malodycatch2ctb-d.plugin.json "${_ctb_manifest}" COPYONLY)
else()
    file(READ plugin/malodycatch2ctb-d.plugin.json _ctb_manifest_text)
    string(REPLACE "malodycatch2ctb-d.exe" "malodycatch2ctb-d" _ctb_manifest_text "${_ctb_manifest_text}")
    file(WRITE "${_ctb_manifest}" "${_ctb_manifest_text}")
endif()

add_custom_command(OUTPUT "${_ctb_binary}"
    COMMAND "${CATCHEDITOR_LDC_COMPILER}" -O3 -release "-of=${_ctb_binary}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/app.d"
    DEPENDS source/app.d
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building Malody Catch to osu! Catch D process plugin"
    VERBATIM
)

set(_ctb_runtime "$<TARGET_FILE_DIR:CatchChartEditor>/plugins/malodycatch2ctb-d")
add_custom_target(MalodyCatch2CTBPlugin ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ctb_runtime}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ctb_binary}" "${_ctb_runtime}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ctb_manifest}" "${_ctb_runtime}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${_ctb_runtime}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${_ctb_runtime}/"
    DEPENDS "${_ctb_binary}" "${_ctb_manifest}" LICENSE README.md
    VERBATIM
)
add_dependencies(CatchChartEditor MalodyCatch2CTBPlugin)

install(PROGRAMS "${_ctb_binary}" DESTINATION bin/plugins/malodycatch2ctb-d)
install(FILES "${_ctb_manifest}" LICENSE README.md DESTINATION bin/plugins/malodycatch2ctb-d)

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ctb_converter_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.py" "${_ctb_binary}")
        set_tests_properties(ctb_converter_smoke PROPERTIES TIMEOUT 60)
    endif()
endif()
