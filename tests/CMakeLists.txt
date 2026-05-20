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
target_compile_definitions(protost_unit_tests PRIVATE
    PROTOST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
catch_discover_tests(protost_unit_tests)
