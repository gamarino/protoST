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
target_sources(protost_unit_tests PRIVATE
    unit/test_mnt_b1_fixes.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_mnt_b2_fixes.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_mnt_c_numbers.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_t4b_math.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_t3b_mixins.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_t3c_addbehavior.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_t5a_interop.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_cross_runtime_provider.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_ready_stack.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_actor_mailbox_gc.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_gc_cycle.cpp
)
target_sources(protost_unit_tests PRIVATE
    unit/test_hashed_collections.cpp
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

# --- examples smoke layer (Track 9) --------------------------------------
# The examples/ tree (docs(examples): comprehensive runnable example set) is
# also a smoke layer: every example carries an "EXPECT: <text>" directive on
# its first line, so the conformance runner verifies it black-box exactly as
# it does a conformance test. Module-helper files prefixed with `_` are
# importable libraries, not standalone examples, and are excluded.
file(GLOB_RECURSE PROTOST_EXAMPLE_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/examples/*.st")
foreach(ex_file ${PROTOST_EXAMPLE_FILES})
    get_filename_component(ex_name "${ex_file}" NAME)
    string(SUBSTRING "${ex_name}" 0 1 ex_first)
    if(ex_first STREQUAL "_")
        continue()
    endif()
    file(RELATIVE_PATH ex_rel "${CMAKE_SOURCE_DIR}/examples" "${ex_file}")
    add_test(NAME "examples/${ex_rel}"
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_conformance.sh
                $<TARGET_FILE:protost>
                "${ex_file}")
endforeach()

# --- documentation examples ------------------------------------------------
# Every example in the documents below that states its result is run and
# checked (tests/docs/run_doc_snippets.py; conventions at the top of it).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(PROTOST_DOC_FILES README.md docs/LANGUAGE.md docs/TUTORIAL.md
        examples/README.md lib/README.md docs/INTEROP.md)
    file(GLOB PROTOST_TUTORIAL_CHAPTERS CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/docs/tutorial/*.md")
    list(APPEND PROTOST_DOC_FILES ${PROTOST_TUTORIAL_CHAPTERS})
    foreach(doc_rel ${PROTOST_DOC_FILES})
        add_test(NAME "docs/${doc_rel}"
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/docs/run_doc_snippets.py
                    --root ${CMAKE_SOURCE_DIR} --lib ${CMAKE_SOURCE_DIR}/lib
                    $<TARGET_FILE:protost> ${CMAKE_SOURCE_DIR}/${doc_rel})
    endforeach()
    add_test(NAME docs/selftest
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/docs/test_run_doc_snippets.py
                $<TARGET_FILE:protost> --lib ${CMAKE_SOURCE_DIR}/lib)
endif()
