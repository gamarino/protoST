cmake_minimum_required(VERSION 3.20)
project(protoST VERSION 0.3.0 LANGUAGES CXX)

# Default to an optimised build. Without this a plain `cmake -B build` leaves
# CMAKE_BUILD_TYPE empty — GCC then compiles at -O0, and every casual build
# (and benchmark) runs unoptimised. RelWithDebInfo keeps debug symbols for
# profiling; override with -DCMAKE_BUILD_TYPE=Release for a stripped -O3 build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      This is the only mode that checks protoCore's version and ABI, so it is
#      the only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: the sibling source tree ../protoCore with one of its
#      build directories already populated. Search order build_release, build,
#      build_check - the first directory holding libprotoCore wins, and
#      build_release comes first so a leftover build/ cannot shadow it. Used
#      only when no installed package was found and no prefix was named;
#      refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
#
# The version floor is 2.1: the hashed collections are built on protoCore's
# `ProtoMap` and the hashed-collection helper, and the actor mailboxes on
# `ProtoMPSCQueue` -- all three added in protoCore 2.1.0. The ceiling is the
# next major, because protoCore's major version and its soname move together.
set(PROTOCORE_MIN_VERSION      "2.1")
set(PROTOCORE_MIN_VERSION_FULL "2.1.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoST is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    message(STATUS "protoST: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoST: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    # The result is cached in PROTOCORE_LIBRARY: pass -DPROTOCORE_LIBRARY=<path>
    # to choose explicitly, and delete the cache entry to search again.
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH)
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR
            "protoCore shared library not found. Build it first:\n"
            "  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIB_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIB_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIB_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
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
    src/primitives/atom_prims.cpp
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

# D23 regression: un-drained actor mailbox load must not deadlock the
# scheduler. Non-deterministic when broken, so the test repeats the workload.
add_test(NAME cli_actor_stress
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_actor_stress.sh $<TARGET_FILE:protost>)

# D25 regression: several workers running a method for the first time at the
# same moment must not corrupt the heap. Non-deterministic when broken, so the
# test repeats the workload.
add_test(NAME cli_concurrent_first_call
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_concurrent_first_call.sh $<TARGET_FILE:protost>)

# S4 regression: blocking waits and the shutdown join while actors allocate
# must not hang or corrupt memory. Repeated.
add_test(NAME cli_gc_shutdown_stress
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_gc_shutdown_stress.sh $<TARGET_FILE:protost>)

# S15 regression: a workload that allocates far more than the heap ceiling
# allows must finish, which it can only do if collections reclaim. The fixture
# also runs it with the interpreter's safepoint hook disabled and requires that
# run to abort, so it cannot pass by accident.
add_test(NAME cli_gc_reclaims
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_gc_reclaims.sh $<TARGET_FILE:protost>)

# Track S: actor message payloads that only the mailbox references must survive
# the collections that run while they are queued. Needs S15 fixed to mean
# anything; the fixture checks that its own ceiling still forces collections.
add_test(NAME cli_actor_payload_gc
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_actor_payload_gc.sh $<TARGET_FILE:protost>)

# S4 regression: a DAP session (run to completion, and a breakpoint stop with
# evaluations) while the debuggee's actors allocate.
add_test(NAME cli_dap_gc
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap_gc.sh $<TARGET_FILE:protost>)

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
    # macOS drag-and-drop .dmg.
    # UNVERIFIED: configured and reviewed, never built - no macOS host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing, which would take the TGZ down with it.
    # Same pattern as protoCore's own CPack block.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOST_DPKG_EXECUTABLE dpkg)
    find_program(PROTOST_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOST_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOST_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # protoST links libprotoCore — the package metadata must declare it.
    # protoCore's package is named "protocore" for DEB and "protoCore" for RPM,
    # both pinned in protoCore/CMakeLists.txt rather than inferred from each
    # generator's default casing. Its major version and its soname move
    # together, so the dependency is bounded on both sides.
    set(CPACK_DEBIAN_PACKAGE_NAME "protost")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoST")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP.
    # UNVERIFIED: configured and reviewed, never built - no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoST")
    set(CPACK_NSIS_PACKAGE_NAME "protoST")
endif()

include(CPack)
