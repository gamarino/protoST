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

# --- nlohmann/json: DAP message (de)serialization (F8) ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindOrFetchNlohmannJson)

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
    src/runtime/HandlerStack.cpp
    src/runtime/ValueFormat.cpp
    src/debugger/DebuggerRuntime.cpp
    src/debugger/BreakpointTable.cpp
    src/primitives/int_prims.cpp
    src/primitives/math_prims.cpp
    src/primitives/time_prims.cpp
    src/primitives/bool_prims.cpp
    src/primitives/string_prims.cpp
    src/primitives/block_prims.cpp
    src/primitives/debugger_prims.cpp
    src/primitives/object_prims.cpp
    src/primitives/future_prims.cpp
    src/primitives/exception_prims.cpp
    src/primitives/collection_prims.cpp
    src/modules/STModuleProvider.cpp
    src/repl/Repl.cpp
    src/dap/DapTransport.cpp
    src/dap/DapServer.cpp
)
target_include_directories(protost_runtime PUBLIC src include ${PROTOCORE_INCLUDE_DIRS})
target_link_libraries(protost_runtime PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(protost_runtime PRIVATE
    PROTOST_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/src/venv_template")

# --- libreadline: line editing + history for the interactive REPL (F7) ---
find_library(READLINE_LIBRARY NAMES readline REQUIRED)
find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
if(READLINE_INCLUDE_DIR)
    target_include_directories(protost_runtime PRIVATE ${READLINE_INCLUDE_DIR})
endif()
target_link_libraries(protost_runtime PUBLIC ${READLINE_LIBRARY})

add_executable(protost src/main.cpp)
target_link_libraries(protost PRIVATE protost_frontend protost_runtime ${PROTOCORE_LIBRARY} ${READLINE_LIBRARY})

enable_testing()

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

add_test(NAME cli_debugger
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_debugger.sh $<TARGET_FILE:protost> ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_test(NAME cli_repl
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_repl.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dap
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dap_session
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap_session.sh $<TARGET_FILE:protost>)

# --- Track 10: Installation packaging ---------------------------------------
# Install RPATH so an installed `bin/protost` finds libprotoCore.so under
# <prefix>/lib without LD_LIBRARY_PATH / DYLD_LIBRARY_PATH (mirrors protoPython).
if(UNIX)
    if(APPLE)
        set_target_properties(protost PROPERTIES
            INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
    else()
        set_target_properties(protost PROPERTIES
            INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
endif()

# The `protost` binary.
install(TARGETS protost
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT protoST)

# The standard-library `.st` modules. They are installed to
# <prefix>/share/protoST/lib; the stdlib discovery in src/runtime/STRuntime.cpp
# probes an install-relative <exe>/../share/protoST/lib so an installed
# `bin/protost` resolves `Import from: 'stream'` with no PROTOST_LIB set.
install(DIRECTORY lib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/protoST/lib
    COMPONENT protoST
    FILES_MATCHING PATTERN "*.st" PATTERN "README.md")

# License and documentation.
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT protoST)
install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT protoST
    FILES_MATCHING PATTERN "*.md")

# VS Code editor integration (DAP debugger glue), if present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/editor-integration/vscode")
    install(DIRECTORY editor-integration/vscode
        DESTINATION ${CMAKE_INSTALL_DATADIR}/protoST/editor-integration
        COMPONENT protoST)
endif()

# --- CPack configuration ----------------------------------------------------
set(CPACK_PACKAGE_NAME "protost")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "protoST - a Smalltalk-flavoured language on the protoCore runtime")
set(CPACK_PACKAGE_VENDOR "numaes")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoST)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "protoST")

# Platform-specific generators.
if(APPLE)
    # macOS drag-and-drop .dmg (unverified on a Linux build host).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    # protoST links libprotoCore — the package metadata must declare it.
    # protoCore's CPack package name is "protoCore"; the DEB generator
    # lowercases it to "protocore", the RPM generator keeps the case.
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "protocore")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES "protoCore")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP (unverified on a Linux build host).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoST")
    set(CPACK_NSIS_PACKAGE_NAME "protoST")
endif()

include(CPack)
