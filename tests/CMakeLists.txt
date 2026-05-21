add_executable(protost_unit_tests
    unit/test_sanity.cpp
)
target_include_directories(protost_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(protost_unit_tests PRIVATE
    Catch2::Catch2WithMain
    protost_frontend
    protost_runtime
    ${PROTOCORE_LIBRARY}
)
target_sources(protost_unit_tests PRIVATE
    unit/test_lexer.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_parser.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_ast_printer.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_venv.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_bytecode_module.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_compiler.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_execution_engine.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_bootstrap.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_debugger.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_worker_thread.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_line_mapping.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_wide_operands.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_dap_transport.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_non_local_return.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_exceptions.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_closure_capture.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_collections.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_conformance_fixes.cpp
)
target_compile_definitions(protost_unit_tests PRIVATE
    PROTOST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
catch_discover_tests(protost_unit_tests)

# --- conformance suite (Track 6, slice 2) --------------------------------
# A defensible suite derived from docs/LANGUAGE.md. Every tests/conformance/
# **/*.st program is registered as one CTest case, run black-box through the
# protost binary by run_conformance.sh. Module-helper files prefixed with `_`
# are importable libraries, not standalone tests, and are excluded.
file(GLOB_RECURSE PROTOST_CONFORMANCE_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.st")
foreach(conf_file ${PROTOST_CONFORMANCE_FILES})
    get_filename_component(conf_name "${conf_file}" NAME)
    string(SUBSTRING "${conf_name}" 0 1 conf_first)
    if(conf_first STREQUAL "_")
        continue()
    endif()
    file(RELATIVE_PATH conf_rel
        "${CMAKE_CURRENT_SOURCE_DIR}/conformance" "${conf_file}")
    add_test(NAME "conformance/${conf_rel}"
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_conformance.sh
                $<TARGET_FILE:protost>
                "${conf_file}")
endforeach()
