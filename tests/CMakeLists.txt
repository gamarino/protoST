add_executable(protost_unit_tests
    unit/test_sanity.cpp
)
target_include_directories(protost_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(protost_unit_tests PRIVATE
    Catch2::Catch2WithMain
    ${PROTOCORE_LIBRARY}
)
target_sources(protost_unit_tests PRIVATE
    unit/test_lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/Lexer.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/Parser.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/AST.cpp
)
target_compile_definitions(protost_unit_tests PRIVATE
    PROTOST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
catch_discover_tests(protost_unit_tests)
