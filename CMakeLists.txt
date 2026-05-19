cmake_minimum_required(VERSION 3.20)
project(protoST VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# --- protoCore: same pattern as protoJS/protoPython ---
if(DEFINED PROTO_CORE_PREFIX)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTO_CORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include headers)
    if(NOT (PROTOCORE_LIBRARY AND PROTO_CORE_INCLUDE_DIR))
        message(FATAL_ERROR "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found.")
    endif()
    set(PROTOCORE_INCLUDE_DIRS "${PROTO_CORE_INCLUDE_DIR}")
else()
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build -S . && cmake --build build --target protoCore")
    endif()
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIB_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

include_directories(${PROTOCORE_INCLUDE_DIRS} include)

add_library(protost_frontend STATIC
    src/frontend/Lexer.cpp
    src/frontend/Parser.cpp
    src/frontend/AST.cpp
    src/frontend/ASTPrinter.cpp
    src/frontend/Compiler.cpp
)
target_include_directories(protost_frontend PUBLIC
    src
    include
    ${PROTOCORE_INCLUDE_DIRS})

add_library(protost_runtime STATIC
    src/runtime/Venv.cpp
    src/runtime/BytecodeModule.cpp
    src/runtime/STRuntime.cpp
    src/runtime/ExecutionEngine.cpp
    src/runtime/Bootstrap.cpp
    src/debugger/DebuggerRuntime.cpp
    src/primitives/int_prims.cpp
    src/primitives/bool_prims.cpp
    src/primitives/string_prims.cpp
    src/primitives/block_prims.cpp
    src/primitives/debugger_prims.cpp
)
target_include_directories(protost_runtime PUBLIC src include ${PROTOCORE_INCLUDE_DIRS})
target_compile_definitions(protost_runtime PRIVATE
    PROTOST_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/src/venv_template")

add_executable(protost src/main.cpp)
target_link_libraries(protost PRIVATE protost_frontend protost_runtime ${PROTOCORE_LIBRARY})

enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindOrFetchCatch2)
add_subdirectory(tests)

add_test(NAME cli_help
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_help.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dump_ast
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dump_ast.sh
            $<TARGET_FILE:protost>
            ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_test(NAME cli_venv
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_venv.sh $<TARGET_FILE:protost>)

add_test(NAME cli_eval
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_eval.sh $<TARGET_FILE:protost>)
