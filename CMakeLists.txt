cmake_minimum_required(VERSION 3.20)
project(protoST VERSION 0.5.0 LANGUAGES CXX)

# The one place the version is written: every binary and library reads it
# through PROTOST_VERSION (include/protoST/STRuntime.h).
add_compile_definitions(PROTOST_VERSION="${PROJECT_VERSION}")

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

# Windows (MSVC) differences, gathered here so the targets below read the same
# on every platform. Outside Windows nothing in this block applies: the build
# keeps the compiler defaults, flags and libraries it always used.
if(MSVC)
    # Sources are UTF-8; the interpreter's big translation units need /bigobj.
    add_compile_options(/utf-8 /bigobj /W3)
    # M_PI and M_E from <cmath>; no min/max macros from <windows.h>.
    # getenv, fopen & co. are standard C here: no "unsafe" deprecation notes.
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
    # 8 MiB stacks, the Linux default the engine's nesting limit is sized for
    # (kMaxNestedEngines, ExecutionEngine.cpp). The executable's reserve is
    # also what Windows gives every thread created without a size, including
    # protoCore's worker threads; the default would be 1 MiB.
    add_link_options(/STACK:8388608)
endif()
if(WIN32)
    # Windows has no rpath: a program finds its DLLs next to itself, so every
    # executable and DLL of the build goes to one directory.
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endif()

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
# The version floor is 2.6.1: the worker pool grows while actors block in I/O
# (STRuntime::enterBlockingIO), which creates threads from worker threads, and
# before 2.6.1 protoCore's newThread detached the main program's roots when
# called there. (2.1.0 brought ProtoMap, the hashed-collection helper and
# ProtoMPSCQueue; 2.6.0 the process-global mutable table.) The ceiling is the
# next major, because protoCore's major version and its soname move together.
set(PROTOCORE_MIN_VERSION      "2.6.1")
set(PROTOCORE_MIN_VERSION_FULL "2.6.1")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "3")
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
    src/runtime/Interrupt.cpp
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
    src/primitives/io_prims.cpp
    src/modules/STModuleProvider.cpp
    src/repl/Repl.cpp
    src/dap/DapTransport.cpp
    src/dap/DapServer.cpp
)
target_include_directories(protost_runtime PUBLIC src include ${PROTOCORE_INCLUDE_DIRS})
target_link_libraries(protost_runtime PUBLIC nlohmann_json::nlohmann_json)

# --- protoIO: files, processes, TCP, UDP, TLS and HTTP (io_prims.cpp) -------
# The POSIX I/O layer shared by the protoCore runtimes, linked statically, so
# the package gains no dependency on protoIO itself. Resolution order, the same
# as for protoCore: an installed CMake package (the protoio-dev deb, or any
# prefix given with -DCMAKE_PREFIX_PATH / a build tree given with
# -DprotoIO_DIR), else the sibling source tree ../protoIO built as part of
# this one. OpenSSL (TLS for sockets and https) comes with protoIO's PUBLIC
# link interface; dpkg-shlibdeps turns it into the package's libssl
# dependency.
find_package(protoIO 0.1 CONFIG QUIET)
if(protoIO_FOUND)
    message(STATUS "protoST: installed protoIO ${protoIO_VERSION} from ${protoIO_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../protoIO/CMakeLists.txt")
    message(STATUS "protoST: no installed protoIO package found; building the sibling ../protoIO")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../protoIO"
                     "${CMAKE_CURRENT_BINARY_DIR}/_protoIO" EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR
        "protoIO not found: install the protoio-dev package, or check out "
        "https://github.com/gamarino/protoIO next to protoST (../protoIO).")
endif()
target_link_libraries(protost_runtime PRIVATE protoIO::protoio)
target_compile_definitions(protost_runtime PRIVATE
    PROTOST_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/src/venv_template")

# --- libreadline: line editing + history for the interactive REPL (F7) ---
if(WIN32)
    # Optional on Windows only: the console edits the line and keeps a history
    # itself, so without readline the REPL reads plain lines there
    # (src/repl/Repl.cpp, PROTOST_NO_READLINE).
    find_library(READLINE_LIBRARY NAMES readline)
    find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
    if(NOT READLINE_LIBRARY OR NOT READLINE_INCLUDE_DIR)
        message(STATUS "protoST: no readline; the REPL uses the console's own line editing")
        set(READLINE_LIBRARY "")
        set(READLINE_INCLUDE_DIR "")
        target_compile_definitions(protost_runtime PRIVATE PROTOST_NO_READLINE)
    endif()
else()
    find_library(READLINE_LIBRARY NAMES readline REQUIRED)
    find_path(READLINE_INCLUDE_DIR NAMES readline/readline.h)
endif()
if(READLINE_INCLUDE_DIR)
    target_include_directories(protost_runtime PRIVATE ${READLINE_INCLUDE_DIR})
endif()
target_link_libraries(protost_runtime PUBLIC ${READLINE_LIBRARY})

# Windows finds a DLL next to the program that loads it. Copy the DLLs protoST
# needs -- protoCore, and OpenSSL through protoIO -- into the build's bin/, so
# protost and the tests run in place. A target the runtime library depends on,
# not a post-build step of protost: the unit tests are run to list their cases
# as soon as they are linked.
if(WIN32)
    set(PROTOST_RUNTIME_DLLS "")
    if(TARGET ${PROTOCORE_LIBRARY})
        list(APPEND PROTOST_RUNTIME_DLLS "$<TARGET_FILE:${PROTOCORE_LIBRARY}>")
    endif()
    if(OPENSSL_INCLUDE_DIR)
        get_filename_component(PROTOST_OPENSSL_BIN "${OPENSSL_INCLUDE_DIR}/../bin" ABSOLUTE)
        file(GLOB PROTOST_OPENSSL_DLLS
            "${PROTOST_OPENSSL_BIN}/libcrypto*.dll" "${PROTOST_OPENSSL_BIN}/libssl*.dll")
        list(APPEND PROTOST_RUNTIME_DLLS ${PROTOST_OPENSSL_DLLS})
    endif()
    add_custom_target(protost_runtime_dlls
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROTOST_RUNTIME_DLLS}
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        VERBATIM)
    add_dependencies(protost_runtime protost_runtime_dlls)
endif()

add_executable(protost src/main.cpp)
if(WIN32)
    # UTF-8 process code page: argv, getenv and paths are UTF-8 as elsewhere.
    target_sources(protost PRIVATE src/windows/utf8.manifest)
endif()
target_link_libraries(protost PRIVATE protost_frontend protost_runtime ${PROTOCORE_LIBRARY} ${READLINE_LIBRARY})

enable_testing()

# The script tests are bash scripts. Windows cannot run a script directly, so
# there they run through the bash of Git for Windows (never System32\bash.exe,
# which is WSL); elsewhere the prefix is empty and the script is the command,
# as before.
if(WIN32)
    find_program(PROTOST_BASH bash HINTS "C:/Program Files/Git/bin" NO_DEFAULT_PATH)
    if(NOT PROTOST_BASH)
        message(WARNING "protoST: Git for Windows' bash not found; the script tests cannot run")
        set(PROTOST_BASH bash)
    endif()
    set(PROTOST_SH "${PROTOST_BASH}")
    # Git's POSIX tools (tr, wc, printf, ...), which the documentation's
    # OSProcess examples run; the bash scripts have them on PATH already.
    get_filename_component(PROTOST_POSIX_TOOLS "${PROTOST_BASH}/../../usr/bin" ABSOLUTE)
    find_package(Python3 COMPONENTS Interpreter)
else()
    set(PROTOST_SH "")
endif()

include(FindOrFetchCatch2)
add_subdirectory(tests)

add_test(NAME cli_help
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_help.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dump_ast
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dump_ast.sh
            $<TARGET_FILE:protost>
            ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_test(NAME cli_venv
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_venv.sh $<TARGET_FILE:protost>)

add_test(NAME cli_eval
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_eval.sh $<TARGET_FILE:protost>)

add_test(NAME cli_io
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_io.sh $<TARGET_FILE:protost>)

add_test(NAME cli_script_output
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_script_output.sh $<TARGET_FILE:protost>)

add_test(NAME cli_memory_bounded
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_memory_bounded.sh $<TARGET_FILE:protost>)

add_test(NAME cli_oom_clean
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_oom_clean.sh $<TARGET_FILE:protost>)

add_test(NAME cli_sigint
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_sigint.sh $<TARGET_FILE:protost>)

add_test(NAME cli_inputs
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_inputs.sh $<TARGET_FILE:protost>)

add_test(NAME cli_actor_unobserved_error
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_actor_unobserved_error.sh $<TARGET_FILE:protost>)

add_test(NAME cli_print_nl
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_print_nl.sh $<TARGET_FILE:protost>)

add_test(NAME cli_transcript
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_transcript.sh $<TARGET_FILE:protost>)

add_test(NAME cli_error_location
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_error_location.sh $<TARGET_FILE:protost>)

# The benchmark harness fails on a missing or wrong VERIFIED result. The
# harness and its self-test (fake protost binaries are /bin/sh scripts) are
# POSIX tooling, so the self-test is not registered on Windows.
if(NOT WIN32)
add_test(NAME bench_harness_selftest
    COMMAND python3 ${CMAKE_SOURCE_DIR}/benchmarks/tests/test_harness.py)
endif()

add_test(NAME cli_unhandled_exception_label
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_unhandled_exception_label.sh $<TARGET_FILE:protost>)

add_test(NAME cli_repl_class_across_inputs
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_repl_class_across_inputs.sh $<TARGET_FILE:protost>)

add_test(NAME cli_callback_error_reported
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_callback_error_reported.sh $<TARGET_FILE:protost>)

add_test(NAME cli_buried_actor_deadlock
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_buried_actor_deadlock.sh $<TARGET_FILE:protost>)

add_test(NAME cli_kernel
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_kernel.sh $<TARGET_FILE:protost> ${CMAKE_BINARY_DIR}/cli-scratch/kernel)

add_test(NAME cli_debugger
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_debugger.sh $<TARGET_FILE:protost> ${CMAKE_SOURCE_DIR}/tests/fixtures)

add_test(NAME cli_repl
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_repl.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dap
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap.sh $<TARGET_FILE:protost>)

add_test(NAME cli_dap_session
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap_session.sh $<TARGET_FILE:protost>)

# D23 regression: un-drained actor mailbox load must not deadlock the
# scheduler. Non-deterministic when broken, so the test repeats the workload.
add_test(NAME cli_actor_stress
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_actor_stress.sh $<TARGET_FILE:protost>)

# D25 regression: several workers running a method for the first time at the
# same moment must not corrupt the heap. Non-deterministic when broken, so the
# test repeats the workload.
add_test(NAME cli_concurrent_first_call
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_concurrent_first_call.sh $<TARGET_FILE:protost>)

# S4 regression: blocking waits and the shutdown join while actors allocate
# must not hang or corrupt memory. Repeated.
add_test(NAME cli_gc_shutdown_stress
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_gc_shutdown_stress.sh $<TARGET_FILE:protost>)

# S15 regression: a workload that allocates far more than the heap ceiling
# allows must finish, which it can only do if collections reclaim. The fixture
# also runs it with the interpreter's safepoint hook disabled and requires that
# run to abort, so it cannot pass by accident.
add_test(NAME cli_gc_reclaims
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_gc_reclaims.sh $<TARGET_FILE:protost>)

# Track S: actor message payloads that only the mailbox references must survive
# the collections that run while they are queued. Needs S15 fixed to mean
# anything; the fixture checks that its own ceiling still forces collections.
add_test(NAME cli_actor_payload_gc
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_actor_payload_gc.sh $<TARGET_FILE:protost>)

# S4 regression: a DAP session (run to completion, and a breakpoint stop with
# evaluations) while the debuggee's actors allocate.
add_test(NAME cli_dap_gc
    COMMAND ${PROTOST_SH} ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dap_gc.sh $<TARGET_FILE:protost>)

if(WIN32)
    # The DAP tests drive protost from a Python client; on Windows `python3`
    # is usually the Microsoft Store stub, so they get the Python CMake found.
    set_tests_properties(cli_dap_session cli_dap_gc PROPERTIES
        ENVIRONMENT "PYTHON=${Python3_EXECUTABLE}")
endif()

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
# protoIO is linked in statically, but OpenSSL is a DLL on Windows: install it
# next to protost.exe, where Windows looks for it. (protoCore is a package of
# its own and installs its DLL into its own prefix's bin/.)
if(WIN32 AND PROTOST_OPENSSL_DLLS)
    install(FILES ${PROTOST_OPENSSL_DLLS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT protoST)
endif()

# The standard-library `.st` modules. They are installed to
# <prefix>/share/protoST/lib; the stdlib discovery in src/runtime/STRuntime.cpp
# probes an install-relative <exe>/../share/protoST/lib so an installed
# `bin/protost` resolves `Import from: 'stream'` with no PROTOST_LIB set.
install(DIRECTORY lib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/protoST/lib
    COMPONENT protoST
    FILES_MATCHING PATTERN "*.st" PATTERN "README.md" PATTERN "00-manifest.txt")

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
    "protoST - a runtime for Smalltalk-syntax programs, with actors, on protoCore")
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
    # SHLIBDEPS is meant to add a SONAME dependency (libprotoCore.so.N)
    # derived from the binary's own NEEDED entry, because the version range
    # above is not an ABI check on its own: a protoCore in range but built
    # with an older SOVERSION is accepted by dpkg and then fails at load
    # time (installers-sep2026 report, F4). Verified (in protoClojure, same
    # mechanism) that it does NOT add that dependency here: protoCore's own
    # .deb ships no shlibs/symbols file, so dpkg-shlibdeps has nothing to
    # emit for libprotoCore.so.3 and CPack's default --ignore-missing-info
    # drops it silently. Left on because it is harmless and adds real
    # transitive deps (libc6, libstdc++6, ...); closing F4 needs a shlibs or
    # symbols file in protoCore's own package, not this flag alone.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_RPM_PACKAGE_NAME "protoST")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP. The ZIP was built and checked on
    # Windows 11 (2026-10-01); the NSIS installer has never been built (no
    # NSIS on that host).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoST")
    set(CPACK_NSIS_PACKAGE_NAME "protoST")
endif()

include(CPack)
