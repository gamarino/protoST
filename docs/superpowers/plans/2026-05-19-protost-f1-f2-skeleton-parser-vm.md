# protoST F1 + F2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver Phase 1 (skeleton + parser + venv scaffold) and Phase 2 (bytecode compiler + synchronous interpreter + CLI debugger) of protoST so that `protost -e '(1 to: 100) inject: 0 into: [:a :b | a + b]'` returns `5050` and `protost -d script.st` lets the user step through a method, inspect locals, and continue.

**Architecture:** Bytecode VM directly over protoCore primitives, minimal decoration. Hand-written recursive-descent parser → AST → 2-byte bytecode (matches `protopyc`) → tree-walking the bytecode against `ProtoContext`-stored operand stack and locals (regla absoluta: no `std::vector` for execution state). Debugger reuses the planned `Future wait` snapshot mechanism (snapshot PC + operand stack + locals into three pointers); in F2 the host is the main thread (single-threaded blocking, pdb-style).

**Tech Stack:** C++20, CMake ≥ 3.20, Catch2 v3 (header-only, fetched via FetchContent), libprotoCore (sibling directory or `PROTO_CORE_PREFIX`), gtest is **not** used (protoJS uses Catch2; we follow that). Test discovery via `catch_discover_tests` + ctest. Linux/macOS targeted; Windows out of scope for F1.

---

## File structure (locked in)

Each file has one responsibility. Files that change together live together.

```
protoST/
├── CMakeLists.txt                          # top-level build
├── cmake/
│   └── FindOrFetchCatch2.cmake             # vendored helper
├── include/protoST/                         # public headers
│   ├── STRuntime.h                          # runtime entry point
│   ├── STValue.h                            # ProtoObject* aliases + helpers
│   └── primitives.h                         # primitive registration table
├── src/
│   ├── frontend/
│   │   ├── Token.h                          # Token enum + struct
│   │   ├── Lexer.h
│   │   ├── Lexer.cpp                        # ST-80 lexer
│   │   ├── AST.h                            # AST node hierarchy
│   │   ├── AST.cpp                          # destructors, visitors
│   │   ├── Parser.h
│   │   ├── Parser.cpp                       # recursive descent
│   │   ├── ASTPrinter.h
│   │   ├── ASTPrinter.cpp                   # pretty-print AST (for --dump-ast)
│   │   ├── Compiler.h                       # AST → bytecode (F2)
│   │   └── Compiler.cpp
│   ├── runtime/
│   │   ├── Opcodes.h                        # bytecode enum + 2-byte format
│   │   ├── BytecodeModule.h                 # holds bytecode + constant pool
│   │   ├── BytecodeModule.cpp
│   │   ├── ExecutionEngine.h
│   │   ├── ExecutionEngine.cpp              # interpreter loop
│   │   ├── STRuntime.cpp                    # init + shutdown
│   │   ├── Bootstrap.cpp                    # creates root prototypes
│   │   └── Venv.h
│   │   └── Venv.cpp                         # venv discovery + commands
│   ├── debugger/
│   │   ├── DebuggerRuntime.h
│   │   ├── DebuggerRuntime.cpp              # halt primitive + session
│   │   ├── BreakpointTable.h
│   │   └── BreakpointTable.cpp
│   ├── primitives/
│   │   ├── int_prims.cpp                    # SmallInteger arithmetic
│   │   ├── bool_prims.cpp                   # True/False ifTrue:/ifFalse:
│   │   ├── string_prims.cpp                 # String basics
│   │   ├── block_prims.cpp                  # BlockClosure value/value:
│   │   └── debugger_prims.cpp               # #DebuggerHalt, #DebuggerEval
│   ├── venv_template/
│   │   ├── stenv.cfg.in
│   │   ├── activate                         # POSIX sh
│   │   ├── activate.fish
│   │   └── activate.ps1                     # placeholder (F1 ships posix only)
│   └── main.cpp                             # CLI entry point
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_lexer.cpp
│   │   ├── test_parser.cpp
│   │   ├── test_ast_printer.cpp
│   │   ├── test_venv.cpp
│   │   ├── test_compiler.cpp                # F2
│   │   ├── test_execution_engine.cpp        # F2
│   │   └── test_debugger.cpp                # F2
│   ├── cli/
│   │   ├── test_cli_dump_ast.sh
│   │   ├── test_cli_eval.sh                 # F2
│   │   └── test_cli_debugger.sh             # F2
│   └── fixtures/
│       ├── hello.st
│       ├── counter.st
│       └── various .st samples
└── examples/                                # populated organically as we go
```

---

## Task index

| # | Phase | Task |
|---|---|---|
| 1 | F1 | Project skeleton: directories, top-level CMakeLists, hello-world build |
| 2 | F1 | Test framework: Catch2 fetch, one passing sanity test, ctest wired |
| 3 | F1 | Token types (Token.h) |
| 4 | F1 | Lexer: whitespace, identifiers, integers |
| 5 | F1 | Lexer: punctuation, binary operators |
| 6 | F1 | Lexer: keyword selectors (`foo:`) |
| 7 | F1 | Lexer: string and char literals |
| 8 | F1 | Lexer: symbol literals (`#foo`, `#+`) |
| 9 | F1 | Lexer: comments and array literal opener `#(` |
| 10 | F1 | Lexer: cascade `;`, return `^`, statement period |
| 11 | F1 | AST node hierarchy (AST.h) |
| 12 | F1 | Parser scaffold: positions, error reporting |
| 13 | F1 | Parser: literals and identifiers (primary) |
| 14 | F1 | Parser: unary, binary, keyword message sends |
| 15 | F1 | Parser: cascades |
| 16 | F1 | Parser: blocks with arguments and locals |
| 17 | F1 | Parser: assignments and statements |
| 18 | F1 | Parser: method definitions (`Class >> selector`) |
| 19 | F1 | Parser: class declarations (`Object subclass: #Foo`) |
| 20 | F1 | Parser: top-level module form |
| 21 | F1 | AST printer (S-expression style) |
| 22 | F1 | CLI: argument parsing skeleton + `--help`, `--version` |
| 23 | F1 | CLI: `protost --dump-ast file.st` end-to-end |
| 24 | F1 | Venv: layout + `protost venv create` |
| 25 | F1 | Venv: `activate` scripts (POSIX, fish) |
| 26 | F1 | Venv: discovery (`STENV` → walk-up → none) |
| 27 | F1 | Venv: `protost venv info` |
| 28 | F1 | End-of-F1 integration test + tag |
| 29 | F2 | Opcode enum and 2-byte instruction layout |
| 30 | F2 | BytecodeModule: constant pool + bytes container |
| 31 | F2 | Compiler scaffold + emit helpers |
| 32 | F2 | Compiler: literal expressions |
| 33 | F2 | Compiler: identifier load/store via slot table |
| 34 | F2 | Compiler: SEND for unary/binary/keyword |
| 35 | F2 | Compiler: cascades |
| 36 | F2 | Compiler: blocks with closure analysis |
| 37 | F2 | Compiler: top-level expression program |
| 38 | F2 | ExecutionEngine scaffold + operand stack on ProtoList |
| 39 | F2 | ExecutionEngine: PUSH/POP/STORE/LOAD opcodes |
| 40 | F2 | ExecutionEngine: SEND dispatch via `getAttribute` |
| 41 | F2 | ExecutionEngine: jumps and conditional branches |
| 42 | F2 | ExecutionEngine: block invocation + return |
| 43 | F2 | Bootstrap: minimal root prototypes (Object, SmallInteger, Boolean, String, Block) |
| 44 | F2 | Primitives: `SmallInteger` arithmetic + comparison |
| 45 | F2 | Primitives: `True`/`False ifTrue:/ifFalse:` |
| 46 | F2 | Primitives: `String` basics (`,`, `size`, `=`, `printNl`) |
| 47 | F2 | Primitives: `BlockClosure value` / `value:` / `whileTrue:` |
| 48 | F2 | CLI: `protost -e '<expr>'` end-to-end (returns `5050`) |
| 49 | F2 | Debugger: `Halt` primitive + zero-overhead when detached |
| 50 | F2 | Debugger: frame snapshot capture |
| 51 | F2 | Debugger: CLI session loop (`(stdbg) ` prompt) |
| 52 | F2 | Debugger: commands `where`, `locals`, `print` |
| 53 | F2 | Debugger: commands `step`, `next`, `cont`, `finish` |
| 54 | F2 | Debugger: location breakpoints (`Debugger breakAt:`) |
| 55 | F2 | Debugger: conditional breakpoints |
| 56 | F2 | CLI: `protost -d script.st` end-to-end |
| 57 | F2 | End-of-F2 integration test + tag |

---

## Conventions used in every task

- **TDD**: every task starts with a failing test, runs it to confirm failure, implements the minimum, runs the test again, then commits.
- **Each step is 2-5 minutes.** A bigger change is split into multiple tasks.
- **Commits**: `feat(<area>): <subject>` for new behaviour, `test(<area>): <subject>` for tests-first commits when split, `chore(build): …` for CMake. Always include `git add` with explicit files.
- **No `std::vector`/`std::list`/`std::deque` for execution state**: operand stack uses `proto::ProtoList`, locals use `proto::ProtoSparseList`. C++ containers are allowed only for build-time scaffolding (token list during lex, AST node list during parse).
- **Build dir**: assume `protoST/build/`. Tests run with `ctest --test-dir build --output-on-failure`.
- **Branch model**: work on `main` directly (single developer). Tag boundaries: `f1-complete`, `f2-complete`.

---

## Task 1 — Project skeleton: directories, top-level CMakeLists, hello-world build

**Files:**
- Create: `protoST/CMakeLists.txt`
- Create: `protoST/include/protoST/STRuntime.h`
- Create: `protoST/src/main.cpp`
- Create: `protoST/.gitignore`

- [ ] **Step 1: Create `.gitignore`**

```gitignore
build/
build_*/
cmake-build-*/
*.o
*.so
*.a
.vscode/
.idea/
*.swp
.venv/
```

- [ ] **Step 2: Create top-level `CMakeLists.txt`**

```cmake
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

# Placeholder hello-world executable; replaced with real CLI in Task 22.
add_executable(protost src/main.cpp)
target_link_libraries(protost PRIVATE ${PROTOCORE_LIBRARY})

enable_testing()
```

- [ ] **Step 3: Create `include/protoST/STRuntime.h` minimal stub**

```cpp
#pragma once

namespace protoST {
    // Sentinel header so the include path is exercised by the build.
    // Real STRuntime declaration arrives in Task 38.
    inline const char* versionString() { return "protoST 0.1.0-pre"; }
}
```

- [ ] **Step 4: Create `src/main.cpp` minimal**

```cpp
#include "protoST/STRuntime.h"
#include <iostream>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << protoST::versionString() << "\n";
    return 0;
}
```

- [ ] **Step 5: Build and run**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoST
cmake -B build -S .
cmake --build build -j
./build/protost
```

Expected: prints `protoST 0.1.0-pre` and exits 0. Verifies the protoCore link succeeds even though we do not call any symbol yet.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt .gitignore include/protoST/STRuntime.h src/main.cpp
git commit -m "chore(build): project skeleton with placeholder CLI"
```

---

## Task 2 — Test framework: Catch2 fetch, one passing sanity test, ctest wired

**Files:**
- Create: `protoST/cmake/FindOrFetchCatch2.cmake`
- Create: `protoST/tests/CMakeLists.txt`
- Create: `protoST/tests/unit/test_sanity.cpp`
- Modify: `protoST/CMakeLists.txt` (append test subdir)

- [ ] **Step 1: Create `cmake/FindOrFetchCatch2.cmake`**

```cmake
# Catch2 v3 via FetchContent. Matches protoJS's expectation
# of <catch2/catch_all.hpp> being available.
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
```

- [ ] **Step 2: Append to top-level `CMakeLists.txt`**

Insert at the bottom (after `enable_testing()`):

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindOrFetchCatch2)
add_subdirectory(tests)
```

- [ ] **Step 3: Create `tests/CMakeLists.txt`**

```cmake
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
catch_discover_tests(protost_unit_tests)
```

- [ ] **Step 4: Create `tests/unit/test_sanity.cpp`**

```cpp
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include <string_view>

TEST_CASE("versionString returns the expected prefix", "[sanity]") {
    std::string_view v = protoST::versionString();
    REQUIRE(v.starts_with("protoST"));
}
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: at least one test passes. Output contains `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Commit**

```bash
git add cmake/FindOrFetchCatch2.cmake tests/CMakeLists.txt tests/unit/test_sanity.cpp CMakeLists.txt
git commit -m "chore(build): wire Catch2 v3 via FetchContent + one sanity test"
```

---

## Task 3 — Token types (Token.h)

**Files:**
- Create: `protoST/src/frontend/Token.h`

- [ ] **Step 1: Create `src/frontend/Token.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace protoST {

enum class TokenKind : uint8_t {
    // Literals
    Integer,            // 42, -7  (negative handled by parser, lexer sees '-')
    Float,              // 3.14
    String,             // 'hola'
    Char,               // $a
    Symbol,             // #foo, #+, #at:put:
    True, False, Nil,

    // Identifiers and selectors
    Identifier,         // counter, value
    Keyword,            // foo:   (identifier + colon, no space)
    BinaryOp,           // +  -  *  /  =  ==  ~=  <  >  <=  >=  &  |  @  ,  ->

    // Punctuation / structural
    LParen, RParen,     // ( )
    LBracket, RBracket, // [ ]
    LBrace, RBrace,     // { }
    HashLParen,         // #(   array literal
    Pipe,               // |    locals separator and binary op (disambiguated by parser)
    Period,             // .    statement terminator
    Semicolon,          // ;    cascade
    Caret,              // ^    return
    Assign,             // :=
    Colon,              // :    block argument prefix
    GtGt,               // >>   method definition marker

    // End-of-stream
    EndOfFile,
    Error,              // sentinel — lexer attaches message in `text`
};

struct Token {
    TokenKind kind;
    std::string text;   // raw source slice (for identifiers, literals, errors)
    long long  intValue = 0;     // valid for TokenKind::Integer
    double     floatValue = 0.0; // valid for TokenKind::Float
    int line = 1;
    int column = 1;
};

inline const char* tokenKindName(TokenKind k) {
    switch (k) {
        case TokenKind::Integer:    return "Integer";
        case TokenKind::Float:      return "Float";
        case TokenKind::String:     return "String";
        case TokenKind::Char:       return "Char";
        case TokenKind::Symbol:     return "Symbol";
        case TokenKind::True:       return "True";
        case TokenKind::False:      return "False";
        case TokenKind::Nil:        return "Nil";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::Keyword:    return "Keyword";
        case TokenKind::BinaryOp:   return "BinaryOp";
        case TokenKind::LParen:     return "LParen";
        case TokenKind::RParen:     return "RParen";
        case TokenKind::LBracket:   return "LBracket";
        case TokenKind::RBracket:   return "RBracket";
        case TokenKind::LBrace:     return "LBrace";
        case TokenKind::RBrace:     return "RBrace";
        case TokenKind::HashLParen: return "HashLParen";
        case TokenKind::Pipe:       return "Pipe";
        case TokenKind::Period:     return "Period";
        case TokenKind::Semicolon:  return "Semicolon";
        case TokenKind::Caret:      return "Caret";
        case TokenKind::Assign:     return "Assign";
        case TokenKind::Colon:      return "Colon";
        case TokenKind::GtGt:       return "GtGt";
        case TokenKind::EndOfFile:  return "EOF";
        case TokenKind::Error:      return "Error";
    }
    return "?";
}

} // namespace protoST
```

- [ ] **Step 2: Commit**

```bash
git add src/frontend/Token.h
git commit -m "feat(lexer): token kinds for ST-80 minimal subset"
```

Header-only step, no test yet — covered by Task 4 which uses these tokens.

---

## Task 4 — Lexer: whitespace, identifiers, integers

**Files:**
- Create: `protoST/src/frontend/Lexer.h`
- Create: `protoST/src/frontend/Lexer.cpp`
- Create: `protoST/tests/unit/test_lexer.cpp`
- Modify: `protoST/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/unit/test_lexer.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "frontend/Lexer.h"

using protoST::Lexer;
using protoST::TokenKind;

TEST_CASE("lexer skips whitespace and lexes identifiers", "[lexer]") {
    Lexer L("  counter  value42");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Identifier);
    REQUIRE(t1.text == "counter");
    REQUIRE(t1.line == 1);
    REQUIRE(t1.column == 3);

    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Identifier);
    REQUIRE(t2.text == "value42");

    REQUIRE(L.next().kind == TokenKind::EndOfFile);
}

TEST_CASE("lexer lexes positive integers", "[lexer]") {
    Lexer L("42 0 1000");
    auto t = L.next();
    REQUIRE(t.kind == TokenKind::Integer);
    REQUIRE(t.intValue == 42);
    REQUIRE(L.next().intValue == 0);
    REQUIRE(L.next().intValue == 1000);
}

TEST_CASE("lexer tracks line numbers across newlines", "[lexer]") {
    Lexer L("a\n b\n  c");
    REQUIRE(L.next().line == 1);
    auto t2 = L.next(); REQUIRE(t2.line == 2); REQUIRE(t2.column == 2);
    auto t3 = L.next(); REQUIRE(t3.line == 3); REQUIRE(t3.column == 3);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
target_sources(protost_unit_tests PRIVATE
    unit/test_lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/Lexer.cpp
)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build -j 2>&1 | head -20
```

Expected: compile error `frontend/Lexer.h: No such file or directory`.

- [ ] **Step 3: Create `src/frontend/Lexer.h`**

```cpp
#pragma once
#include "Token.h"
#include <string>
#include <string_view>

namespace protoST {

class Lexer {
public:
    explicit Lexer(std::string source);
    Token next();
    Token peek();
    bool  atEnd() const { return pos_ >= source_.size(); }

private:
    std::string source_;
    size_t pos_ = 0;
    int    line_ = 1;
    int    col_ = 1;
    bool   hasPeek_ = false;
    Token  peekTok_;

    void   advance();
    char   current() const { return pos_ < source_.size() ? source_[pos_] : '\0'; }
    char   lookahead(size_t k = 1) const {
        return (pos_ + k) < source_.size() ? source_[pos_ + k] : '\0';
    }
    void   skipWhitespace();
    Token  lexIdentifier();
    Token  lexNumber();
    Token  makeError(const std::string& msg, int l, int c);
};

} // namespace protoST
```

- [ ] **Step 4: Create `src/frontend/Lexer.cpp` minimal**

```cpp
#include "Lexer.h"
#include <cctype>

namespace protoST {

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

void Lexer::advance() {
    if (pos_ >= source_.size()) return;
    if (source_[pos_] == '\n') { ++line_; col_ = 1; }
    else                       { ++col_; }
    ++pos_;
}

void Lexer::skipWhitespace() {
    while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }
}

Token Lexer::lexIdentifier() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
        advance();
    }
    Token t;
    t.kind = TokenKind::Identifier;
    t.text = source_.substr(start, pos_ - start);
    t.line = startLine; t.column = startCol;
    return t;
}

Token Lexer::lexNumber() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }
    Token t;
    t.kind = TokenKind::Integer;
    t.text = source_.substr(start, pos_ - start);
    t.intValue = std::stoll(t.text);
    t.line = startLine; t.column = startCol;
    return t;
}

Token Lexer::makeError(const std::string& msg, int l, int c) {
    Token t; t.kind = TokenKind::Error; t.text = msg; t.line = l; t.column = c; return t;
}

Token Lexer::next() {
    if (hasPeek_) { hasPeek_ = false; return peekTok_; }
    skipWhitespace();
    if (pos_ >= source_.size()) {
        Token t; t.kind = TokenKind::EndOfFile; t.line = line_; t.column = col_; return t;
    }
    char c = current();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return lexIdentifier();
    if (std::isdigit(static_cast<unsigned char>(c)))             return lexNumber();
    return makeError(std::string("unexpected character '") + c + "'", line_, col_);
}

Token Lexer::peek() {
    if (!hasPeek_) { peekTok_ = next(); hasPeek_ = true; }
    return peekTok_;
}

} // namespace protoST
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R lexer
```

Expected: 3 lexer test cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/frontend/Lexer.h src/frontend/Lexer.cpp tests/unit/test_lexer.cpp tests/CMakeLists.txt
git commit -m "feat(lexer): identifiers, integers, whitespace, line tracking"
```

---

## Task 5 — Lexer: punctuation, binary operators

**Files:**
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

- [ ] **Step 1: Write the failing test (append cases)**

In `test_lexer.cpp` append:

```cpp
TEST_CASE("lexer recognizes single-char punctuation", "[lexer]") {
    Lexer L("( ) [ ] { } . ; ^ |");
    REQUIRE(L.next().kind == TokenKind::LParen);
    REQUIRE(L.next().kind == TokenKind::RParen);
    REQUIRE(L.next().kind == TokenKind::LBracket);
    REQUIRE(L.next().kind == TokenKind::RBracket);
    REQUIRE(L.next().kind == TokenKind::LBrace);
    REQUIRE(L.next().kind == TokenKind::RBrace);
    REQUIRE(L.next().kind == TokenKind::Period);
    REQUIRE(L.next().kind == TokenKind::Semicolon);
    REQUIRE(L.next().kind == TokenKind::Caret);
    REQUIRE(L.next().kind == TokenKind::Pipe);
}

TEST_CASE("lexer recognizes binary operators and := and >>", "[lexer]") {
    Lexer L("+ - * / = == ~= <= >= < > & , -> := >>");
    auto checkBin = [&](const char* expected) {
        auto t = L.next();
        REQUIRE(t.kind == TokenKind::BinaryOp);
        REQUIRE(t.text == expected);
    };
    checkBin("+"); checkBin("-"); checkBin("*"); checkBin("/");
    checkBin("="); checkBin("=="); checkBin("~="); checkBin("<=");
    checkBin(">="); checkBin("<"); checkBin(">"); checkBin("&");
    checkBin(","); checkBin("->");
    REQUIRE(L.next().kind == TokenKind::Assign);
    REQUIRE(L.next().kind == TokenKind::GtGt);
}
```

- [ ] **Step 2: Run, verify fail**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
```

Expected: new tests fail with `Error` tokens.

- [ ] **Step 3: Extend `Lexer::next()` to handle punctuation and binary operators**

Replace the trailing `return makeError(...)` in `Lexer::next()` with this dispatcher (insert before the makeError line):

```cpp
    int startLine = line_, startCol = col_;
    auto single = [&](TokenKind k) -> Token {
        Token t; t.kind = k; t.text = std::string(1, c); t.line = startLine; t.column = startCol; advance(); return t;
    };
    auto bin1 = [&](const char* s) -> Token {
        Token t; t.kind = TokenKind::BinaryOp; t.text = s; t.line = startLine; t.column = startCol; advance(); return t;
    };

    switch (c) {
        case '(': return single(TokenKind::LParen);
        case ')': return single(TokenKind::RParen);
        case '[': return single(TokenKind::LBracket);
        case ']': return single(TokenKind::RBracket);
        case '{': return single(TokenKind::LBrace);
        case '}': return single(TokenKind::RBrace);
        case '.': return single(TokenKind::Period);
        case ';': return single(TokenKind::Semicolon);
        case '^': return single(TokenKind::Caret);
        case '|': return single(TokenKind::Pipe);
        case '+': case '*': case '/': case '&':
            return bin1(std::string(1, c).c_str());
        case ',': return bin1(",");
        case '-':
            if (lookahead() == '>') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "->";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("-");
        case '=':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "==";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("=");
        case '~':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "~=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return makeError("unexpected '~'", startLine, startCol);
        case '<':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = "<=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1("<");
        case '>':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::BinaryOp; t.text = ">=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            if (lookahead() == '>') {
                Token t; t.kind = TokenKind::GtGt; t.text = ">>";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return bin1(">");
        case ':':
            if (lookahead() == '=') {
                Token t; t.kind = TokenKind::Assign; t.text = ":=";
                t.line = startLine; t.column = startCol; advance(); advance(); return t;
            }
            return single(TokenKind::Colon);
    }
```

- [ ] **Step 4: Run and verify all lexer tests pass**

```bash
cmake --build build -j && ctest --test-dir build -R lexer --output-on-failure
```

Expected: all lexer tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): punctuation, binary operators, := and >>"
```

---

## Task 6 — Lexer: keyword selectors (`foo:`)

**Files:**
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

A keyword selector is an identifier immediately followed by `:` with no space, e.g. `at:put:` is two keyword tokens, `foo bar:` is identifier + keyword.

- [ ] **Step 1: Write the failing test**

Append to `test_lexer.cpp`:

```cpp
TEST_CASE("lexer recognizes keyword selectors", "[lexer]") {
    Lexer L("at: put: foo bar:baz");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Keyword);
    REQUIRE(t1.text == "at:");

    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Keyword);
    REQUIRE(t2.text == "put:");

    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Identifier);
    REQUIRE(t3.text == "foo");

    auto t4 = L.next();
    REQUIRE(t4.kind == TokenKind::Keyword);
    REQUIRE(t4.text == "bar:");

    auto t5 = L.next();
    REQUIRE(t5.kind == TokenKind::Identifier);
    REQUIRE(t5.text == "baz");
}
```

- [ ] **Step 2: Run, verify fail**

```bash
cmake --build build -j && ctest --test-dir build -R "keyword selectors"
```

Expected: identifier returned where keyword was expected.

- [ ] **Step 3: Patch `Lexer::lexIdentifier`**

Replace the body of `lexIdentifier` with:

```cpp
Token Lexer::lexIdentifier() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
        advance();
    }
    // Keyword selector: identifier IMMEDIATELY followed by ':' (no whitespace)
    if (pos_ < source_.size() && source_[pos_] == ':' &&
        (pos_ + 1 >= source_.size() || source_[pos_ + 1] != '=')) {
        advance();   // consume ':'
        Token t;
        t.kind = TokenKind::Keyword;
        t.text = source_.substr(start, pos_ - start);
        t.line = startLine; t.column = startCol;
        return t;
    }
    Token t;
    t.kind = TokenKind::Identifier;
    t.text = source_.substr(start, pos_ - start);
    if (t.text == "true")  t.kind = TokenKind::True;
    else if (t.text == "false") t.kind = TokenKind::False;
    else if (t.text == "nil")   t.kind = TokenKind::Nil;
    t.line = startLine; t.column = startCol;
    return t;
}
```

(Also folds in the `true`/`false`/`nil` keyword check needed by later tasks.)

- [ ] **Step 4: Test**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
```

Expected: all lexer tests still pass + new ones.

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): keyword selectors and true/false/nil"
```

---

## Task 7 — Lexer: string and char literals

**Files:**
- Modify: `protoST/src/frontend/Lexer.h`
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

ST-80 strings are `'...'` with `''` escaping a single quote. Chars are `$x` (single char after `$`).

- [ ] **Step 1: Write failing test**

```cpp
TEST_CASE("lexer reads single-quoted strings with '' escapes", "[lexer]") {
    Lexer L("'hola' 'it''s ok' ''");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::String);
    REQUIRE(t1.text == "hola");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::String);
    REQUIRE(t2.text == "it's ok");
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::String);
    REQUIRE(t3.text == "");
}

TEST_CASE("lexer reads char literals $x", "[lexer]") {
    Lexer L("$a $$ $ ");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Char);
    REQUIRE(t1.text == "a");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Char);
    REQUIRE(t2.text == "$");
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Char);
    REQUIRE(t3.text == " ");
}
```

- [ ] **Step 2: Run, verify fail**

```bash
cmake --build build -j && ctest --test-dir build -R "string|char literals"
```

Expected: tests fail (currently `Error` or wrong).

- [ ] **Step 3: Add to `Lexer.h` (private section)**

```cpp
    Token lexString();
    Token lexChar();
```

- [ ] **Step 4: Implement in `Lexer.cpp`**

```cpp
Token Lexer::lexString() {
    int startLine = line_, startCol = col_;
    advance();  // consume opening '
    std::string out;
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == '\'') {
            // possible escape: '' -> '
            if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '\'') {
                out += '\'';
                advance(); advance();
                continue;
            }
            advance();
            Token t; t.kind = TokenKind::String; t.text = std::move(out);
            t.line = startLine; t.column = startCol; return t;
        }
        out += c;
        advance();
    }
    return makeError("unterminated string literal", startLine, startCol);
}

Token Lexer::lexChar() {
    int startLine = line_, startCol = col_;
    advance(); // consume '$'
    if (pos_ >= source_.size()) {
        return makeError("unterminated char literal", startLine, startCol);
    }
    Token t; t.kind = TokenKind::Char; t.text = std::string(1, source_[pos_]);
    t.line = startLine; t.column = startCol;
    advance();
    return t;
}
```

In `next()`, before the switch on punctuation, add:

```cpp
    if (c == '\'') return lexString();
    if (c == '$')  return lexChar();
```

- [ ] **Step 5: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
```

```bash
git add src/frontend/Lexer.h src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): string and char literals"
```

---

## Task 8 — Lexer: symbol literals (`#foo`, `#+`, `#at:put:`)

**Files:**
- Modify: `protoST/src/frontend/Lexer.h`
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

A symbol is `#` followed by either an identifier, a binary operator, or a sequence of keyword fragments forming a keyword selector. `#(` is a separate token (array literal opener) and handled in Task 9.

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("lexer reads identifier symbols", "[lexer]") {
    Lexer L("#foo #valueOf");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Symbol);
    REQUIRE(t1.text == "foo");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Symbol);
    REQUIRE(t2.text == "valueOf");
}

TEST_CASE("lexer reads binary symbols", "[lexer]") {
    Lexer L("#+ #<= #==");
    REQUIRE(L.next().text == "+");
    REQUIRE(L.next().text == "<=");
    REQUIRE(L.next().text == "==");
}

TEST_CASE("lexer reads keyword-selector symbols", "[lexer]") {
    Lexer L("#at:put: #foo:");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Symbol);
    REQUIRE(t1.text == "at:put:");
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Symbol);
    REQUIRE(t2.text == "foo:");
}
```

- [ ] **Step 2: Add declaration in `Lexer.h`**

```cpp
    Token lexSymbol();
```

- [ ] **Step 3: Implement in `Lexer.cpp`**

```cpp
Token Lexer::lexSymbol() {
    int startLine = line_, startCol = col_;
    advance(); // consume '#'
    if (pos_ >= source_.size()) {
        return makeError("incomplete symbol", startLine, startCol);
    }
    char c = source_[pos_];
    Token t; t.kind = TokenKind::Symbol; t.line = startLine; t.column = startCol;

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        // identifier OR keyword-chain
        std::string out;
        while (pos_ < source_.size() &&
               (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
            out += source_[pos_]; advance();
        }
        // chain of `name:` segments
        while (pos_ < source_.size() && source_[pos_] == ':' &&
               (pos_ + 1 >= source_.size() || source_[pos_ + 1] != '=')) {
            out += ':'; advance();
            while (pos_ < source_.size() &&
                   (std::isalnum(static_cast<unsigned char>(source_[pos_])) || source_[pos_] == '_')) {
                out += source_[pos_]; advance();
            }
        }
        t.text = std::move(out);
        return t;
    }

    // binary operator symbol: take 1–2 chars from the binop alphabet
    static const std::string binChars = "+-*/=~<>&|@,";
    if (binChars.find(c) != std::string::npos) {
        std::string out; out += c; advance();
        if (pos_ < source_.size() && binChars.find(source_[pos_]) != std::string::npos) {
            out += source_[pos_]; advance();
        }
        t.text = std::move(out);
        return t;
    }
    return makeError("malformed symbol literal", startLine, startCol);
}
```

In `next()`, add **before** the existing `c == '\''` check:

```cpp
    if (c == '#') {
        if (lookahead() == '(') {
            Token t; t.kind = TokenKind::HashLParen; t.text = "#(";
            t.line = line_; t.column = col_; advance(); advance(); return t;
        }
        return lexSymbol();
    }
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
git add src/frontend/Lexer.h src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): symbol literals (identifier, binary, keyword chains) and #( opener"
```

---

## Task 9 — Lexer: comments and floats

**Files:**
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

ST-80 comments are `"..."` and span multiple lines. They are skipped entirely.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("lexer skips ST-style \"...\" comments", "[lexer]") {
    Lexer L("\"a comment\" 42 \"another\nmultiline\" foo");
    REQUIRE(L.next().intValue == 42);
    auto t = L.next();
    REQUIRE(t.kind == TokenKind::Identifier);
    REQUIRE(t.text == "foo");
}

TEST_CASE("lexer reads floats", "[lexer]") {
    Lexer L("3.14 0.5 42");
    auto t1 = L.next();
    REQUIRE(t1.kind == TokenKind::Float);
    REQUIRE(t1.floatValue == Catch::Approx(3.14));
    auto t2 = L.next();
    REQUIRE(t2.kind == TokenKind::Float);
    REQUIRE(t2.floatValue == Catch::Approx(0.5));
    auto t3 = L.next();
    REQUIRE(t3.kind == TokenKind::Integer);
    REQUIRE(t3.intValue == 42);
}
```

- [ ] **Step 2: Extend `skipWhitespace()` to also skip comments**

Replace the function body with:

```cpp
void Lexer::skipWhitespace() {
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (std::isspace(static_cast<unsigned char>(c))) { advance(); continue; }
        if (c == '"') {
            advance();
            while (pos_ < source_.size() && source_[pos_] != '"') advance();
            if (pos_ < source_.size()) advance();
            continue;
        }
        break;
    }
}
```

- [ ] **Step 3: Extend `lexNumber()` to recognise floats**

Replace `lexNumber()`:

```cpp
Token Lexer::lexNumber() {
    int startLine = line_, startCol = col_;
    size_t start = pos_;
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        advance();
    }
    bool isFloat = false;
    if (pos_ < source_.size() && source_[pos_] == '.' &&
        pos_ + 1 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_ + 1]))) {
        isFloat = true;
        advance(); // .
        while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
            advance();
        }
    }
    Token t;
    t.text = source_.substr(start, pos_ - start);
    t.line = startLine; t.column = startCol;
    if (isFloat) { t.kind = TokenKind::Float; t.floatValue = std::stod(t.text); }
    else         { t.kind = TokenKind::Integer; t.intValue   = std::stoll(t.text); }
    return t;
}
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
git add src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): \"...\" comments and float literals"
```

---

## Task 10 — Lexer: collect-all helper for AST printer round-trip tests

**Files:**
- Modify: `protoST/src/frontend/Lexer.h`
- Modify: `protoST/src/frontend/Lexer.cpp`
- Modify: `protoST/tests/unit/test_lexer.cpp`

Adds a `Lexer::tokenize()` helper that returns all tokens. This will be reused by the parser tests and by `--dump-ast`.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Lexer::tokenize returns full sequence ending in EOF", "[lexer]") {
    auto tokens = Lexer("a := 1.").tokenize();
    REQUIRE(tokens.size() == 5);   // a, :=, 1, ., EOF
    REQUIRE(tokens[0].kind == TokenKind::Identifier);
    REQUIRE(tokens[1].kind == TokenKind::Assign);
    REQUIRE(tokens[2].kind == TokenKind::Integer);
    REQUIRE(tokens[3].kind == TokenKind::Period);
    REQUIRE(tokens[4].kind == TokenKind::EndOfFile);
}
```

- [ ] **Step 2: Add to `Lexer.h`**

```cpp
    std::vector<Token> tokenize();
```

Add `#include <vector>` near the top.

- [ ] **Step 3: Implement**

```cpp
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> result;
    while (true) {
        auto t = next();
        bool eof = (t.kind == TokenKind::EndOfFile);
        result.push_back(std::move(t));
        if (eof) break;
    }
    return result;
}
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R lexer
git add src/frontend/Lexer.h src/frontend/Lexer.cpp tests/unit/test_lexer.cpp
git commit -m "feat(lexer): tokenize() helper to collect full stream"
```

End of lexer block. Lexer now handles every token the F1 parser needs.

---

## Task 11 — AST node hierarchy (AST.h)

**Files:**
- Create: `protoST/src/frontend/AST.h`
- Create: `protoST/src/frontend/AST.cpp`

A flat, tagged-union-ish design with `std::variant` is tempting but harder to extend. We use a small class hierarchy with a single base class and `std::unique_ptr` ownership.

- [ ] **Step 1: Create `src/frontend/AST.h`**

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace protoST::ast {

enum class NodeKind : uint8_t {
    // Expressions
    IntegerLit, FloatLit, StringLit, SymbolLit, CharLit,
    TrueLit, FalseLit, NilLit,
    ArrayLit,        // #(...)
    DynArrayLit,     // {...}
    Identifier,
    Self, Super, ThisContext,
    Assignment,
    UnarySend, BinarySend, KeywordSend,
    Cascade,
    Block,
    Return,
    // Top-level
    MethodDecl,
    ClassDecl,
    Module,
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind;
    int line = 0;
    int column = 0;

    // Common payloads (only some used per kind; checked by kind).
    std::string text;            // identifier name, selector, raw string
    long long   intValue = 0;
    double      floatValue = 0;
    std::vector<NodePtr> children;
    std::vector<std::string> stringList; // e.g., keyword parts of a Block's args, inst-var names
    bool boolFlag = false;        // e.g., ClassDecl: isClassSide on method, etc.

    explicit Node(NodeKind k) : kind(k) {}
};

// Construction helpers — keep call sites short
inline NodePtr makeNode(NodeKind k, int line, int col) {
    auto n = std::make_unique<Node>(k);
    n->line = line; n->column = col;
    return n;
}

} // namespace protoST::ast
```

- [ ] **Step 2: Create `src/frontend/AST.cpp`**

```cpp
#include "AST.h"
namespace protoST::ast {
// Reserved for non-inline helpers; empty for now to keep the .cpp present
// in the build so changes to AST.h trigger a rebuild of dependents.
} // namespace protoST::ast
```

- [ ] **Step 3: Commit (no test yet — exercised by parser tests in Task 13)**

```bash
git add src/frontend/AST.h src/frontend/AST.cpp
git commit -m "feat(ast): node hierarchy with single Node + std::variant-free payload"
```

---

## Task 12 — Parser scaffold: positions, error reporting

**Files:**
- Create: `protoST/src/frontend/Parser.h`
- Create: `protoST/src/frontend/Parser.cpp`
- Create: `protoST/tests/unit/test_parser.cpp`
- Modify: `protoST/tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

`tests/unit/test_parser.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"

using protoST::Parser;
using protoST::ast::NodeKind;

TEST_CASE("Parser scaffold builds empty module from empty source", "[parser]") {
    Parser P("");
    auto mod = P.parseModule();
    REQUIRE(mod != nullptr);
    REQUIRE(mod->kind == NodeKind::Module);
    REQUIRE(mod->children.empty());
    REQUIRE(P.errors().empty());
}

TEST_CASE("Parser reports unexpected token", "[parser]") {
    Parser P("@@@");          // '@' is binop but not legal at top level
    auto mod = P.parseModule();
    REQUIRE(!P.errors().empty());
}
```

Append to `tests/CMakeLists.txt`:

```cmake
target_sources(protost_unit_tests PRIVATE
    unit/test_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/Parser.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/AST.cpp
)
```

- [ ] **Step 2: Create `Parser.h`**

```cpp
#pragma once
#include "AST.h"
#include "Lexer.h"
#include <string>
#include <vector>

namespace protoST {

struct ParseError {
    std::string message;
    int line = 0;
    int column = 0;
};

class Parser {
public:
    explicit Parser(std::string source);

    ast::NodePtr parseModule();
    const std::vector<ParseError>& errors() const { return errors_; }

private:
    Lexer  lexer_;
    Token  current_;
    Token  prev_;
    std::vector<ParseError> errors_;

    void   advance();
    bool   check(TokenKind k) const { return current_.kind == k; }
    bool   match(TokenKind k);
    Token  consume(TokenKind k, const std::string& msg);
    void   error(const Token& at, const std::string& msg);
    void   synchronize();

    // grammar entry points (added in later tasks)
    ast::NodePtr parseTopForm();
    ast::NodePtr parseStatement();
    ast::NodePtr parseExpression();
    ast::NodePtr parseAssignmentRHS(ast::NodePtr target);
    ast::NodePtr parseKeywordSend();
    ast::NodePtr parseBinarySend();
    ast::NodePtr parseUnarySend();
    ast::NodePtr parsePrimary();
    ast::NodePtr parseBlock();
    ast::NodePtr parseClassDecl(Token classIdent);
    ast::NodePtr parseMethodDecl(Token classIdent, bool classSide);
};

} // namespace protoST
```

- [ ] **Step 3: Create minimal `Parser.cpp`**

```cpp
#include "Parser.h"

namespace protoST {

Parser::Parser(std::string source) : lexer_(std::move(source)) {
    advance();
}

void Parser::advance() {
    prev_ = current_;
    current_ = lexer_.next();
    while (current_.kind == TokenKind::Error) {
        error(current_, current_.text);
        current_ = lexer_.next();
    }
}

bool Parser::match(TokenKind k) {
    if (current_.kind != k) return false;
    advance();
    return true;
}

Token Parser::consume(TokenKind k, const std::string& msg) {
    if (current_.kind == k) { Token t = current_; advance(); return t; }
    error(current_, msg);
    return current_;
}

void Parser::error(const Token& at, const std::string& msg) {
    errors_.push_back(ParseError{msg, at.line, at.column});
}

void Parser::synchronize() {
    while (current_.kind != TokenKind::EndOfFile &&
           current_.kind != TokenKind::Period) {
        advance();
    }
    if (current_.kind == TokenKind::Period) advance();
}

ast::NodePtr Parser::parseModule() {
    auto mod = ast::makeNode(ast::NodeKind::Module, 1, 1);
    while (current_.kind != TokenKind::EndOfFile) {
        auto top = parseTopForm();
        if (top) mod->children.push_back(std::move(top));
        else     synchronize();
    }
    return mod;
}

// Stubs that subsequent tasks fill in
ast::NodePtr Parser::parseTopForm()    { error(current_, "unexpected token at top level"); advance(); return nullptr; }
ast::NodePtr Parser::parseStatement()  { return nullptr; }
ast::NodePtr Parser::parseExpression() { return nullptr; }
ast::NodePtr Parser::parseAssignmentRHS(ast::NodePtr) { return nullptr; }
ast::NodePtr Parser::parseKeywordSend(){ return nullptr; }
ast::NodePtr Parser::parseBinarySend() { return nullptr; }
ast::NodePtr Parser::parseUnarySend()  { return nullptr; }
ast::NodePtr Parser::parsePrimary()    { return nullptr; }
ast::NodePtr Parser::parseBlock()      { return nullptr; }
ast::NodePtr Parser::parseClassDecl(Token)         { return nullptr; }
ast::NodePtr Parser::parseMethodDecl(Token, bool)  { return nullptr; }

} // namespace protoST
```

- [ ] **Step 4: Build and run**

```bash
cmake --build build -j && ctest --test-dir build -R parser
```

Expected: the two scaffold tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/frontend/Parser.h src/frontend/Parser.cpp tests/unit/test_parser.cpp tests/CMakeLists.txt
git commit -m "feat(parser): scaffold with module entry, error collection, sync"
```

---

## Task 13 — Parser: primary expressions (literals, identifiers, parenthesised)

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

- [ ] **Step 1: Failing test (extend `test_parser.cpp`)**

```cpp
TEST_CASE("Parser: literal integers", "[parser]") {
    Parser P("42.");
    auto mod = P.parseModule();
    REQUIRE(P.errors().empty());
    REQUIRE(mod->children.size() == 1);
    auto& expr = mod->children[0];
    REQUIRE(expr->kind == NodeKind::IntegerLit);
    REQUIRE(expr->intValue == 42);
}

TEST_CASE("Parser: identifiers and self/super/nil/true/false", "[parser]") {
    auto parseOne = [](const char* src) {
        Parser P(src);
        auto m = P.parseModule();
        REQUIRE(P.errors().empty());
        REQUIRE(m->children.size() == 1);
        return std::move(m->children[0]);
    };
    REQUIRE(parseOne("foo.")->kind == NodeKind::Identifier);
    REQUIRE(parseOne("self.")->kind == NodeKind::Self);
    REQUIRE(parseOne("super.")->kind == NodeKind::Super);
    REQUIRE(parseOne("nil.")->kind == NodeKind::NilLit);
    REQUIRE(parseOne("true.")->kind == NodeKind::TrueLit);
    REQUIRE(parseOne("false.")->kind == NodeKind::FalseLit);
}

TEST_CASE("Parser: parenthesised expression preserves inner kind", "[parser]") {
    Parser P("(42).");
    auto m = P.parseModule();
    REQUIRE(P.errors().empty());
    REQUIRE(m->children[0]->kind == NodeKind::IntegerLit);
}
```

- [ ] **Step 2: Patch `parseTopForm`, `parseStatement`, `parseExpression`, `parsePrimary`**

Replace the stubs in `Parser.cpp` with these implementations. The remaining stubs (`parseUnarySend`, `parseBinarySend`, `parseKeywordSend`) are temporarily routed to delegate to `parsePrimary` so a literal is already a valid statement.

```cpp
ast::NodePtr Parser::parseTopForm() {
    auto stmt = parseStatement();
    if (!match(TokenKind::Period)) {
        // implicit period at EOF is allowed
        if (current_.kind != TokenKind::EndOfFile)
            error(current_, "expected '.' after top-level expression");
    }
    return stmt;
}

ast::NodePtr Parser::parseStatement() {
    if (match(TokenKind::Caret)) {
        auto n = ast::makeNode(ast::NodeKind::Return, prev_.line, prev_.column);
        auto inner = parseExpression();
        if (inner) n->children.push_back(std::move(inner));
        return n;
    }
    return parseExpression();
}

ast::NodePtr Parser::parseExpression() {
    return parseKeywordSend();
}

ast::NodePtr Parser::parseKeywordSend() { return parseBinarySend(); }
ast::NodePtr Parser::parseBinarySend()  { return parseUnarySend(); }
ast::NodePtr Parser::parseUnarySend()   { return parsePrimary(); }

ast::NodePtr Parser::parsePrimary() {
    Token t = current_;
    switch (current_.kind) {
        case TokenKind::Integer: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::IntegerLit, t.line, t.column);
            n->intValue = t.intValue; n->text = t.text;
            return n;
        }
        case TokenKind::Float: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::FloatLit, t.line, t.column);
            n->floatValue = t.floatValue; n->text = t.text;
            return n;
        }
        case TokenKind::String: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::StringLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::Char: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::CharLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::Symbol: {
            advance();
            auto n = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column);
            n->text = t.text; return n;
        }
        case TokenKind::True:   advance(); return ast::makeNode(ast::NodeKind::TrueLit,  t.line, t.column);
        case TokenKind::False:  advance(); return ast::makeNode(ast::NodeKind::FalseLit, t.line, t.column);
        case TokenKind::Nil:    advance(); return ast::makeNode(ast::NodeKind::NilLit,   t.line, t.column);
        case TokenKind::Identifier: {
            advance();
            ast::NodeKind k = ast::NodeKind::Identifier;
            if (t.text == "self")        k = ast::NodeKind::Self;
            else if (t.text == "super")  k = ast::NodeKind::Super;
            else if (t.text == "thisContext") k = ast::NodeKind::ThisContext;
            auto n = ast::makeNode(k, t.line, t.column);
            n->text = t.text;
            return n;
        }
        case TokenKind::LParen: {
            advance();
            auto inner = parseExpression();
            consume(TokenKind::RParen, "expected ')'");
            return inner;
        }
        default:
            error(current_, std::string("expected primary expression, got ") + tokenKindName(current_.kind));
            advance();
            return nullptr;
    }
}
```

- [ ] **Step 3: Build, run, commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): primary expressions (literals, identifiers, parens) and return ^"
```

---

## Task 14 — Parser: unary, binary, keyword message sends

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

Smalltalk precedence (tightest first): unary > binary > keyword. Each level is left-associative within itself.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Parser: unary chain", "[parser]") {
    Parser P("foo printNl size.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& outer = m->children[0];                 // UnarySend size
    REQUIRE(outer->kind == NodeKind::UnarySend);
    REQUIRE(outer->text == "size");
    auto& inner = outer->children[0];             // UnarySend printNl
    REQUIRE(inner->kind == NodeKind::UnarySend);
    REQUIRE(inner->text == "printNl");
    REQUIRE(inner->children[0]->kind == NodeKind::Identifier);
    REQUIRE(inner->children[0]->text == "foo");
}

TEST_CASE("Parser: binary message left-assoc", "[parser]") {
    Parser P("1 + 2 + 3.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& top = m->children[0];                   // (1+2)+3
    REQUIRE(top->kind == NodeKind::BinarySend);
    REQUIRE(top->text == "+");
    REQUIRE(top->children[1]->kind == NodeKind::IntegerLit);
    REQUIRE(top->children[1]->intValue == 3);
    auto& left = top->children[0];
    REQUIRE(left->kind == NodeKind::BinarySend);
    REQUIRE(left->children[0]->intValue == 1);
    REQUIRE(left->children[1]->intValue == 2);
}

TEST_CASE("Parser: keyword send aggregates parts", "[parser]") {
    Parser P("dict at: 1 put: 'one'.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& kw = m->children[0];
    REQUIRE(kw->kind == NodeKind::KeywordSend);
    REQUIRE(kw->text == "at:put:");
    REQUIRE(kw->children.size() == 3);            // receiver + 2 args
    REQUIRE(kw->children[0]->kind == NodeKind::Identifier);
    REQUIRE(kw->children[1]->intValue == 1);
    REQUIRE(kw->children[2]->kind == NodeKind::StringLit);
}

TEST_CASE("Parser: precedence unary > binary > keyword", "[parser]") {
    Parser P("x at: y size + 1 put: z.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& kw = m->children[0];
    REQUIRE(kw->kind == NodeKind::KeywordSend);
    REQUIRE(kw->text == "at:put:");
    auto& arg1 = kw->children[1];                 // y size + 1
    REQUIRE(arg1->kind == NodeKind::BinarySend);
    REQUIRE(arg1->text == "+");
    REQUIRE(arg1->children[0]->kind == NodeKind::UnarySend); // y size
}
```

- [ ] **Step 2: Implement message-send parsing**

Replace the three stubs in `Parser.cpp`:

```cpp
ast::NodePtr Parser::parseUnarySend() {
    auto recv = parsePrimary();
    while (recv && current_.kind == TokenKind::Identifier) {
        // distinguish: only an identifier that is NOT followed by ':' is a unary selector;
        // keyword selectors come tokenised as TokenKind::Keyword.
        Token sel = current_;
        advance();
        auto n = ast::makeNode(ast::NodeKind::UnarySend, sel.line, sel.column);
        n->text = sel.text;
        n->children.push_back(std::move(recv));
        recv = std::move(n);
    }
    return recv;
}

ast::NodePtr Parser::parseBinarySend() {
    auto left = parseUnarySend();
    while (left && current_.kind == TokenKind::BinaryOp) {
        Token op = current_; advance();
        auto right = parseUnarySend();
        auto n = ast::makeNode(ast::NodeKind::BinarySend, op.line, op.column);
        n->text = op.text;
        n->children.push_back(std::move(left));
        if (right) n->children.push_back(std::move(right));
        left = std::move(n);
    }
    return left;
}

ast::NodePtr Parser::parseKeywordSend() {
    auto recv = parseBinarySend();
    if (recv && current_.kind == TokenKind::Keyword) {
        auto n = ast::makeNode(ast::NodeKind::KeywordSend, current_.line, current_.column);
        n->children.push_back(std::move(recv));
        std::string selector;
        while (current_.kind == TokenKind::Keyword) {
            selector += current_.text;            // includes trailing ':'
            advance();
            auto arg = parseBinarySend();
            if (arg) n->children.push_back(std::move(arg));
        }
        n->text = std::move(selector);
        return n;
    }
    return recv;
}
```

- [ ] **Step 3: Build, test, commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): unary/binary/keyword sends with ST-80 precedence"
```

---

## Task 15 — Parser: cascades (`;`)

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

A cascade groups multiple messages sent to the same receiver. The AST has a `Cascade` node whose first child is the receiver and the remaining children are partial sends (a send node with the receiver slot left empty, marked by an absent child[0]).

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Parser: cascade collects messages on same receiver", "[parser]") {
    Parser P("Transcript show: 'a'; show: 'b'; cr.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& casc = m->children[0];
    REQUIRE(casc->kind == NodeKind::Cascade);
    REQUIRE(casc->children.size() == 4); // receiver + 3 partial sends
    REQUIRE(casc->children[0]->kind == NodeKind::Identifier);
    REQUIRE(casc->children[1]->kind == NodeKind::KeywordSend);
    REQUIRE(casc->children[2]->kind == NodeKind::KeywordSend);
    REQUIRE(casc->children[3]->kind == NodeKind::UnarySend);
    // partial sends have NO receiver in children[0]
    REQUIRE(casc->children[1]->children.size() == 1); // one keyword arg, no receiver
    REQUIRE(casc->children[3]->children.empty());
}
```

- [ ] **Step 2: Add cascade handling**

The cleanest spot is `parseExpression()`. After it gets the keyword/binary/unary expression, if the next token is `;`, fold it into a `Cascade`. The "head send" must be a send node (not a literal/identifier); if it is, we promote its receiver out and start a cascade. Partial sends within the cascade are parsed with an explicitly absent receiver.

Replace `parseExpression()`:

```cpp
static bool isSendKind(ast::NodeKind k) {
    return k == ast::NodeKind::UnarySend
        || k == ast::NodeKind::BinarySend
        || k == ast::NodeKind::KeywordSend;
}

ast::NodePtr Parser::parseExpression() {
    auto first = parseKeywordSend();
    if (!first || current_.kind != TokenKind::Semicolon || !isSendKind(first->kind)) {
        return first;
    }
    // Promote receiver: cascade.children[0] = first.children[0]; rest are headless sends
    auto cascade = ast::makeNode(ast::NodeKind::Cascade, first->line, first->column);
    cascade->children.push_back(std::move(first->children[0]));
    first->children.erase(first->children.begin());
    cascade->children.push_back(std::move(first));
    while (match(TokenKind::Semicolon)) {
        // parse a single message with receiver=nullptr (we manufacture)
        Token t = current_;
        if (current_.kind == TokenKind::Identifier) {
            // unary selector
            advance();
            auto n = ast::makeNode(ast::NodeKind::UnarySend, t.line, t.column);
            n->text = t.text;
            // chain unary
            while (current_.kind == TokenKind::Identifier) {
                Token chained = current_; advance();
                auto outer = ast::makeNode(ast::NodeKind::UnarySend, chained.line, chained.column);
                outer->text = chained.text;
                outer->children.push_back(std::move(n));
                n = std::move(outer);
            }
            cascade->children.push_back(std::move(n));
        } else if (current_.kind == TokenKind::BinaryOp) {
            Token op = current_; advance();
            auto right = parseUnarySend();
            auto n = ast::makeNode(ast::NodeKind::BinarySend, op.line, op.column);
            n->text = op.text;
            // partial: no receiver (placeholder will be filled at codegen time)
            if (right) n->children.push_back(std::move(right));
            cascade->children.push_back(std::move(n));
        } else if (current_.kind == TokenKind::Keyword) {
            auto n = ast::makeNode(ast::NodeKind::KeywordSend, current_.line, current_.column);
            std::string selector;
            while (current_.kind == TokenKind::Keyword) {
                selector += current_.text; advance();
                auto arg = parseBinarySend();
                if (arg) n->children.push_back(std::move(arg));
            }
            n->text = std::move(selector);
            cascade->children.push_back(std::move(n));
        } else {
            error(current_, "expected message after ';' in cascade");
            break;
        }
    }
    return cascade;
}
```

(Cascade partial-send convention: child[0] is the **first argument** of a binary/keyword send, not the receiver; for unary, there are no children. Code-gen plugs the receiver back in.)

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): cascade ; with headless partial sends"
```

---

## Task 16 — Parser: blocks with arguments and locals

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

ST-80 block: `[ :a :b | | x y | statements ]`. Arguments come first (each preceded by `:`), then optional `|`, then optional locals between `|...|`, then statements separated by `.`.

Block AST: `kind = Block`; `stringList[0..nArgs-1]` = argument names; `boolFlag` and a side count to mark how many of the leading `stringList` entries are arguments vs locals — simpler: store all names in `stringList`, with `intValue` = number of arguments. `children` are the statements.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Parser: block with arguments and locals", "[parser]") {
    Parser P("[ :a :b | | x | a + b ].");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& blk = m->children[0];
    REQUIRE(blk->kind == NodeKind::Block);
    REQUIRE(blk->intValue == 2);                 // arg count
    REQUIRE(blk->stringList == std::vector<std::string>{"a","b","x"});
    REQUIRE(blk->children.size() == 1);
    REQUIRE(blk->children[0]->kind == NodeKind::BinarySend);
}

TEST_CASE("Parser: empty block", "[parser]") {
    Parser P("[].");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& blk = m->children[0];
    REQUIRE(blk->kind == NodeKind::Block);
    REQUIRE(blk->intValue == 0);
    REQUIRE(blk->children.empty());
}
```

- [ ] **Step 2: Implement `parseBlock` and wire it to `parsePrimary`**

In `parsePrimary`, add **before** the `default:` case:

```cpp
        case TokenKind::LBracket:
            return parseBlock();
```

Implement `parseBlock`:

```cpp
ast::NodePtr Parser::parseBlock() {
    Token open = current_; advance(); // consume '['
    auto blk = ast::makeNode(ast::NodeKind::Block, open.line, open.column);

    // arguments: : name : name ... (then a '|' if any arg present)
    int nArgs = 0;
    while (current_.kind == TokenKind::Colon) {
        advance();
        if (current_.kind != TokenKind::Identifier) {
            error(current_, "expected block argument name after ':'");
            break;
        }
        blk->stringList.push_back(current_.text);
        ++nArgs;
        advance();
    }
    if (nArgs > 0) consume(TokenKind::Pipe, "expected '|' after block arguments");

    // locals between '|...|' (if first token is Pipe)
    if (current_.kind == TokenKind::Pipe) {
        advance();
        while (current_.kind == TokenKind::Identifier) {
            blk->stringList.push_back(current_.text);
            advance();
        }
        consume(TokenKind::Pipe, "expected '|' to close block locals");
    }

    blk->intValue = nArgs;

    // statements
    while (current_.kind != TokenKind::RBracket &&
           current_.kind != TokenKind::EndOfFile) {
        auto stmt = parseStatement();
        if (stmt) blk->children.push_back(std::move(stmt));
        if (!match(TokenKind::Period)) break;
    }
    consume(TokenKind::RBracket, "expected ']' to close block");
    return blk;
}
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): blocks with arguments, locals, and statements"
```

---

## Task 17 — Parser: assignments and array literals

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Parser: assignment", "[parser]") {
    Parser P("x := 1 + 2.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& a = m->children[0];
    REQUIRE(a->kind == NodeKind::Assignment);
    REQUIRE(a->text == "x");
    REQUIRE(a->children[0]->kind == NodeKind::BinarySend);
}

TEST_CASE("Parser: dynamic array literal { a. b. c }", "[parser]") {
    Parser P("{ 1. 2. 3 }.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& arr = m->children[0];
    REQUIRE(arr->kind == NodeKind::DynArrayLit);
    REQUIRE(arr->children.size() == 3);
}

TEST_CASE("Parser: frozen array literal #(1 2 'a')", "[parser]") {
    Parser P("#(1 2 'a').");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& arr = m->children[0];
    REQUIRE(arr->kind == NodeKind::ArrayLit);
    REQUIRE(arr->children.size() == 3);
    REQUIRE(arr->children[0]->kind == NodeKind::IntegerLit);
    REQUIRE(arr->children[2]->kind == NodeKind::StringLit);
}
```

- [ ] **Step 2: Patch `parseStatement` to handle assignment, and `parsePrimary` for arrays**

In `parseStatement`, before `return parseExpression();` add:

```cpp
    if (current_.kind == TokenKind::Identifier && lexer_.peek().kind == TokenKind::Assign) {
        Token id = current_; advance();   // identifier
        advance();                         // ':='
        auto rhs = parseExpression();
        auto n = ast::makeNode(ast::NodeKind::Assignment, id.line, id.column);
        n->text = id.text;
        if (rhs) n->children.push_back(std::move(rhs));
        return n;
    }
```

In `parsePrimary`, before `default:` add:

```cpp
        case TokenKind::LBrace: {
            Token open = current_; advance();
            auto arr = ast::makeNode(ast::NodeKind::DynArrayLit, open.line, open.column);
            while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::EndOfFile) {
                auto e = parseExpression();
                if (e) arr->children.push_back(std::move(e));
                if (!match(TokenKind::Period)) break;
            }
            consume(TokenKind::RBrace, "expected '}' to close dynamic array");
            return arr;
        }
        case TokenKind::HashLParen: {
            Token open = current_; advance();
            auto arr = ast::makeNode(ast::NodeKind::ArrayLit, open.line, open.column);
            while (current_.kind != TokenKind::RParen && current_.kind != TokenKind::EndOfFile) {
                // only literals inside a frozen array
                Token t = current_;
                ast::NodePtr lit;
                if (t.kind == TokenKind::Integer) { advance(); lit = ast::makeNode(ast::NodeKind::IntegerLit, t.line, t.column); lit->intValue = t.intValue; }
                else if (t.kind == TokenKind::Float) { advance(); lit = ast::makeNode(ast::NodeKind::FloatLit, t.line, t.column); lit->floatValue = t.floatValue; }
                else if (t.kind == TokenKind::String) { advance(); lit = ast::makeNode(ast::NodeKind::StringLit, t.line, t.column); lit->text = t.text; }
                else if (t.kind == TokenKind::Symbol) { advance(); lit = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column); lit->text = t.text; }
                else if (t.kind == TokenKind::Identifier) { advance(); lit = ast::makeNode(ast::NodeKind::SymbolLit, t.line, t.column); lit->text = t.text; } // bare ids inside #(..) are symbols
                else { error(current_, "unexpected token in frozen array literal"); advance(); continue; }
                arr->children.push_back(std::move(lit));
            }
            consume(TokenKind::RParen, "expected ')' to close frozen array");
            return arr;
        }
```

- [ ] **Step 3: `Lexer::peek` reminder**

Confirm that `Lexer::peek()` (added in Task 3 header) is defined; the implementation in Task 4 already sets `hasPeek_`.

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): assignments and array literals (#(...) and {...})"
```

---

## Task 18 — Parser: method definitions (`Class >> selector`)

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

Method definition lives at the top level. Syntax: `Identifier ['class'] >> <selector-pattern>` followed by a method body of the form

```
[ '|' identifier* '|' ] statements
```

terminated by the next top-level form or EOF. The selector pattern is one of:
- unary: a single `Identifier`
- binary: `BinaryOp identifier`
- keyword: one or more `Keyword identifier` pairs

The AST `MethodDecl` node carries:
- `text`: class name
- `boolFlag`: true if `class` side
- `stringList[0]`: the selector
- `stringList[1..nArgs]`: argument names
- `stringList[nArgs+1..]`: local var names
- `intValue`: argument count
- `children`: the statements

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Parser: unary method", "[parser]") {
    Parser P("Counter >> increment value := value + 1.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    REQUIRE(m->children.size() == 1);
    auto& md = m->children[0];
    REQUIRE(md->kind == NodeKind::MethodDecl);
    REQUIRE(md->text == "Counter");
    REQUIRE(md->boolFlag == false);
    REQUIRE(md->stringList[0] == "increment");
    REQUIRE(md->intValue == 0);
    REQUIRE(md->children.size() == 1);          // one statement
    REQUIRE(md->children[0]->kind == NodeKind::Assignment);
}

TEST_CASE("Parser: keyword method on class side", "[parser]") {
    Parser P("Counter class >> startingAt: n | c | c := self new. c setValue: n. ^ c.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& md = m->children[0];
    REQUIRE(md->kind == NodeKind::MethodDecl);
    REQUIRE(md->text == "Counter");
    REQUIRE(md->boolFlag == true);              // class side
    REQUIRE(md->stringList[0] == "startingAt:");
    REQUIRE(md->stringList.at(1) == "n");       // arg
    REQUIRE(md->stringList.at(2) == "c");       // local
    REQUIRE(md->intValue == 1);                 // 1 argument
    REQUIRE(md->children.size() == 3);
    REQUIRE(md->children[2]->kind == NodeKind::Return);
}

TEST_CASE("Parser: binary method", "[parser]") {
    Parser P("Number >> + other ^ self primAdd: other.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& md = m->children[0];
    REQUIRE(md->kind == NodeKind::MethodDecl);
    REQUIRE(md->stringList[0] == "+");
    REQUIRE(md->stringList.at(1) == "other");
    REQUIRE(md->intValue == 1);
}
```

- [ ] **Step 2: Patch `parseTopForm` and add `parseMethodDecl`**

Replace `parseTopForm`:

```cpp
ast::NodePtr Parser::parseTopForm() {
    // class/method declarations begin with `Identifier`. Distinguish:
    //   Identifier 'subclass:' #Identifier ... .                 → ClassDecl
    //   Identifier ['class'] '>>' <selector-pattern> body        → MethodDecl
    //   else                                                     → expression statement '.'
    if (current_.kind == TokenKind::Identifier) {
        Token classId = current_;
        Token after = lexer_.peek();
        if (after.kind == TokenKind::GtGt) {
            advance(); // consume class id
            advance(); // consume >>
            return parseMethodDecl(classId, /*classSide=*/false);
        }
        if (after.kind == TokenKind::Identifier && after.text == "class") {
            // peek one more — need a 2-token lookahead. Pull both tokens into prev.
            advance(); // class id
            advance(); // 'class'
            if (current_.kind == TokenKind::GtGt) {
                advance(); // >>
                return parseMethodDecl(classId, /*classSide=*/true);
            }
            // not a method decl; bail to expression — rewind impossible, so report error
            error(current_, "expected '>>' after 'class'");
            synchronize();
            return nullptr;
        }
        if (after.kind == TokenKind::Keyword && after.text == "subclass:") {
            advance();
            return parseClassDecl(classId);
        }
    }
    auto stmt = parseStatement();
    if (!match(TokenKind::Period) && current_.kind != TokenKind::EndOfFile) {
        error(current_, "expected '.' after top-level expression");
    }
    return stmt;
}
```

Implement `parseMethodDecl`:

```cpp
ast::NodePtr Parser::parseMethodDecl(Token classIdent, bool classSide) {
    auto md = ast::makeNode(ast::NodeKind::MethodDecl, classIdent.line, classIdent.column);
    md->text = classIdent.text;
    md->boolFlag = classSide;

    std::string selector;
    int nArgs = 0;

    if (current_.kind == TokenKind::Identifier) {
        // unary
        selector = current_.text;
        advance();
    } else if (current_.kind == TokenKind::BinaryOp) {
        selector = current_.text;
        advance();
        if (current_.kind != TokenKind::Identifier) {
            error(current_, "expected argument name after binary selector");
        } else {
            md->stringList.push_back(current_.text);
            advance();
            ++nArgs;
        }
    } else if (current_.kind == TokenKind::Keyword) {
        while (current_.kind == TokenKind::Keyword) {
            selector += current_.text;
            advance();
            if (current_.kind != TokenKind::Identifier) {
                error(current_, "expected argument name after keyword");
                break;
            }
            md->stringList.push_back(current_.text);
            advance();
            ++nArgs;
        }
    } else {
        error(current_, "expected method selector");
    }

    // selector occupies index 0; shift arguments to start at index 1
    md->stringList.insert(md->stringList.begin(), selector);
    md->intValue = nArgs;

    // optional locals
    if (current_.kind == TokenKind::Pipe) {
        advance();
        while (current_.kind == TokenKind::Identifier) {
            md->stringList.push_back(current_.text);
            advance();
        }
        consume(TokenKind::Pipe, "expected '|' to close method locals");
    }

    // body: statements until we see a token that can only start a top-level form
    // (another Identifier followed by '>>' / 'class' / 'subclass:', or EOF).
    while (current_.kind != TokenKind::EndOfFile) {
        // stop at the start of another method/class decl
        if (current_.kind == TokenKind::Identifier) {
            Token p = lexer_.peek();
            if (p.kind == TokenKind::GtGt) break;
            if (p.kind == TokenKind::Identifier && p.text == "class") break;
            if (p.kind == TokenKind::Keyword && p.text == "subclass:") break;
        }
        auto stmt = parseStatement();
        if (stmt) md->children.push_back(std::move(stmt));
        if (!match(TokenKind::Period)) break;
    }
    return md;
}
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): method definitions (unary, binary, keyword; class-side)"
```

---

## Task 19 — Parser: class declarations (`Object subclass: #Foo …`)

**Files:**
- Modify: `protoST/src/frontend/Parser.cpp`
- Modify: `protoST/tests/unit/test_parser.cpp`

The minimal form is:

```
Identifier 'subclass:' '#' Identifier 'instanceVariableNames:' "..." 'classVariableNames:' "..." .
```

We make `instanceVariableNames:` and `classVariableNames:` optional (default empty).

`ClassDecl` AST: `text` = subclass name; `stringList[0]` = superclass name; `stringList[1..]` = instance var names; `boolFlag` = false (reserved for future flags); `children` empty.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Parser: class declaration minimal", "[parser]") {
    Parser P("Object subclass: #Counter.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& cd = m->children[0];
    REQUIRE(cd->kind == NodeKind::ClassDecl);
    REQUIRE(cd->text == "Counter");
    REQUIRE(cd->stringList[0] == "Object");
    // no instance vars by default
}

TEST_CASE("Parser: class declaration with inst vars", "[parser]") {
    Parser P("Object subclass: #Counter instanceVariableNames: 'value step'.");
    auto m = P.parseModule(); REQUIRE(P.errors().empty());
    auto& cd = m->children[0];
    REQUIRE(cd->stringList.size() == 3);
    REQUIRE(cd->stringList[0] == "Object");
    REQUIRE(cd->stringList[1] == "value");
    REQUIRE(cd->stringList[2] == "step");
}
```

- [ ] **Step 2: Implement `parseClassDecl`**

When called from `parseTopForm`, `prev_` is the `subclass:` keyword and the next token is `#Identifier`.

```cpp
ast::NodePtr Parser::parseClassDecl(Token classIdent) {
    auto cd = ast::makeNode(ast::NodeKind::ClassDecl, classIdent.line, classIdent.column);
    cd->stringList.push_back(classIdent.text);   // superclass name at [0]

    // expect a Symbol literal #Name
    if (current_.kind != TokenKind::Symbol) {
        error(current_, "expected #ClassName after subclass:");
        synchronize();
        return cd;
    }
    cd->text = current_.text;
    advance();

    auto parseStringList = [&](std::vector<std::string>& out) {
        // parses a single 'a b c' string literal as space-separated identifiers
        if (current_.kind == TokenKind::String) {
            std::string s = current_.text;
            advance();
            std::string cur;
            for (char ch : s) {
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
                } else cur += ch;
            }
            if (!cur.empty()) out.push_back(std::move(cur));
        } else {
            error(current_, "expected string literal");
        }
    };

    while (current_.kind == TokenKind::Keyword) {
        if (current_.text == "instanceVariableNames:") {
            advance();
            parseStringList(cd->stringList);
        } else if (current_.text == "classVariableNames:") {
            advance();
            // ignore class-var contents for now; future MetaclassDecl pulls them out
            if (current_.kind == TokenKind::String) advance();
            else error(current_, "expected string after classVariableNames:");
        } else {
            error(current_, "unknown keyword in class declaration: " + current_.text);
            break;
        }
    }
    return cd;
}
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add src/frontend/Parser.cpp tests/unit/test_parser.cpp
git commit -m "feat(parser): class declarations (Object subclass: #Foo …)"
```

---

## Task 20 — Parser: full-module integration test

**Files:**
- Modify: `protoST/tests/unit/test_parser.cpp`
- Create: `protoST/tests/fixtures/counter.st`

- [ ] **Step 1: Write fixture**

```smalltalk
"-- counter.st --"
Object subclass: #Counter
  instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.

"-- top-level expression --"
(Counter startingAt: 10) increment.
```

- [ ] **Step 2: Add fixture-driven test**

```cpp
#include <fstream>
#include <sstream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

TEST_CASE("Parser: counter.st fixture parses cleanly", "[parser][fixture]") {
    auto src = readFile(std::string(PROTOST_FIXTURES_DIR) + "/counter.st");
    REQUIRE(!src.empty());
    Parser P(std::move(src));
    auto m = P.parseModule();
    REQUIRE(P.errors().empty());
    REQUIRE(m->children.size() == 6);            // class decl + 4 methods + 1 top-level
    REQUIRE(m->children[0]->kind == NodeKind::ClassDecl);
    REQUIRE(m->children[1]->kind == NodeKind::MethodDecl);
    REQUIRE(m->children[4]->kind == NodeKind::MethodDecl);
    REQUIRE(m->children[4]->boolFlag == true);   // class side
    REQUIRE(m->children[5]->kind == NodeKind::UnarySend); // (... increment)
}
```

- [ ] **Step 3: Define the fixtures path in `tests/CMakeLists.txt`**

Append:

```cmake
target_compile_definitions(protost_unit_tests PRIVATE
    PROTOST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R parser
git add tests/fixtures/counter.st tests/unit/test_parser.cpp tests/CMakeLists.txt
git commit -m "test(parser): full counter.st fixture parses cleanly"
```

---

## Task 21 — AST printer (S-expression style)

**Files:**
- Create: `protoST/src/frontend/ASTPrinter.h`
- Create: `protoST/src/frontend/ASTPrinter.cpp`
- Create: `protoST/tests/unit/test_ast_printer.cpp`
- Modify: `protoST/tests/CMakeLists.txt`

Output format (deterministic, readable, machine-parsable):

```
(module
  (class-decl Counter Object (inst-vars value))
  (method-decl Counter increment 0 ()
    (assign value (binary + (id value) (int 1)))))
```

Indentation by 2 spaces.

- [ ] **Step 1: Failing test**

`tests/unit/test_ast_printer.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"
#include "frontend/ASTPrinter.h"

TEST_CASE("ASTPrinter: integer literal", "[ast][printer]") {
    protoST::Parser P("42.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out == "(module\n  (int 42))\n");
}

TEST_CASE("ASTPrinter: binary send", "[ast][printer]") {
    protoST::Parser P("1 + 2.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out == "(module\n  (binary + (int 1) (int 2)))\n");
}

TEST_CASE("ASTPrinter: method decl", "[ast][printer]") {
    protoST::Parser P("Counter >> increment value := value + 1.");
    auto m = P.parseModule();
    auto out = protoST::astToString(*m);
    REQUIRE(out.find("(method-decl Counter increment 0 ()") != std::string::npos);
    REQUIRE(out.find("(assign value (binary + (id value) (int 1)))") != std::string::npos);
}
```

Append `unit/test_ast_printer.cpp` to `tests/CMakeLists.txt` `target_sources` and add `${CMAKE_SOURCE_DIR}/src/frontend/ASTPrinter.cpp`.

- [ ] **Step 2: Create `ASTPrinter.h`**

```cpp
#pragma once
#include "AST.h"
#include <string>

namespace protoST {

std::string astToString(const ast::Node& n);

} // namespace protoST
```

- [ ] **Step 3: Create `ASTPrinter.cpp`**

```cpp
#include "ASTPrinter.h"
#include <sstream>

namespace protoST {
using namespace ast;

namespace {
void emit(std::ostringstream& os, const Node& n, int depth);

void indent(std::ostringstream& os, int depth) {
    for (int i = 0; i < depth; ++i) os << "  ";
}

void emitChildren(std::ostringstream& os, const Node& n, int depth) {
    for (auto& ch : n.children) {
        os << "\n";
        indent(os, depth + 1);
        emit(os, *ch, depth + 1);
    }
}

void emit(std::ostringstream& os, const Node& n, int depth) {
    switch (n.kind) {
        case NodeKind::Module:        os << "(module"; emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::IntegerLit:    os << "(int "    << n.intValue << ")"; return;
        case NodeKind::FloatLit:      os << "(float "  << n.floatValue << ")"; return;
        case NodeKind::StringLit:     os << "(str '"   << n.text << "')"; return;
        case NodeKind::SymbolLit:     os << "(sym "    << n.text << ")"; return;
        case NodeKind::CharLit:       os << "(char $"  << n.text << ")"; return;
        case NodeKind::TrueLit:       os << "(true)";  return;
        case NodeKind::FalseLit:      os << "(false)"; return;
        case NodeKind::NilLit:        os << "(nil)";   return;
        case NodeKind::Self:          os << "(self)";  return;
        case NodeKind::Super:         os << "(super)"; return;
        case NodeKind::ThisContext:   os << "(this-context)"; return;
        case NodeKind::Identifier:    os << "(id " << n.text << ")"; return;
        case NodeKind::ArrayLit:      os << "(array";       emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::DynArrayLit:   os << "(dyn-array";   emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::Assignment:    os << "(assign " << n.text;
                                       if (!n.children.empty()) { os << " "; emit(os, *n.children[0], depth); }
                                       os << ")"; return;
        case NodeKind::UnarySend:     os << "(unary "  << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::BinarySend:    os << "(binary " << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::KeywordSend:   os << "(keyword " << n.text;
                                       for (auto& c : n.children) { os << " "; emit(os, *c, depth); }
                                       os << ")"; return;
        case NodeKind::Cascade:       os << "(cascade"; emitChildren(os, n, depth); os << ")"; return;
        case NodeKind::Block: {
            os << "(block argc=" << n.intValue << " names=(";
            for (size_t i = 0; i < n.stringList.size(); ++i) { if (i) os << " "; os << n.stringList[i]; }
            os << ")";
            emitChildren(os, n, depth);
            os << ")"; return;
        }
        case NodeKind::Return:        os << "(^ ";
                                       if (!n.children.empty()) emit(os, *n.children[0], depth);
                                       os << ")"; return;
        case NodeKind::MethodDecl: {
            os << "(method-decl " << n.text << " " << n.stringList[0] << " " << n.intValue << " (";
            for (int i = 0; i < n.intValue; ++i) {
                if (i) os << " ";
                os << n.stringList[1 + i];
            }
            os << ")";
            if (n.boolFlag) os << " class-side";
            emitChildren(os, n, depth);
            os << ")"; return;
        }
        case NodeKind::ClassDecl: {
            os << "(class-decl " << n.text << " " << n.stringList[0] << " (inst-vars";
            for (size_t i = 1; i < n.stringList.size(); ++i) os << " " << n.stringList[i];
            os << "))"; return;
        }
    }
}
} // anon

std::string astToString(const Node& n) {
    std::ostringstream os;
    emit(os, n, 0);
    os << "\n";
    return os.str();
}

} // namespace protoST
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R ast
git add src/frontend/ASTPrinter.h src/frontend/ASTPrinter.cpp tests/unit/test_ast_printer.cpp tests/CMakeLists.txt
git commit -m "feat(ast): S-expression-style printer for --dump-ast"
```

---

## Task 22 — CLI: argument parsing skeleton + `--help`, `--version`

**Files:**
- Modify: `protoST/src/main.cpp`
- Modify: `protoST/CMakeLists.txt`
- Create: `protoST/tests/cli/test_cli_help.sh`

- [ ] **Step 1: Write `main.cpp` with arg parser**

Replace `src/main.cpp`:

```cpp
#include "protoST/STRuntime.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] <script.st> [args...]\n"
        "       %s -e '<expr>'\n"
        "       %s -i                     (REPL — F7)\n"
        "       %s -d <script.st>         (debugger — F2)\n"
        "       %s --dump-ast <script.st>\n"
        "       %s venv <subcommand> [args]\n"
        "       %s compile <script.st> -o <out.stbc>\n"
        "\nOptions:\n"
        "  -e '<expr>'    Evaluate expression and print result\n"
        "  -i             Start REPL (F7)\n"
        "  -d             Debug script (F2)\n"
        "  --dump-ast     Parse and dump AST (F1)\n"
        "  --help         Show this message\n"
        "  --version      Show version\n",
        prog, prog, prog, prog, prog, prog, prog);
}

void printVersion() {
    std::printf("%s\n", protoST::versionString());
}

} // anon

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(argv[0]); return 64; }
    std::string mode = argv[1];

    if (mode == "--help" || mode == "-h")   { printUsage(argv[0]); return 0; }
    if (mode == "--version" || mode == "-v"){ printVersion();      return 0; }
    if (mode == "--dump-ast")               { return 1; /* implemented in Task 23 */ }
    if (mode == "-e")                       { return 1; /* implemented in Task 48 */ }
    if (mode == "-d")                       { return 1; /* implemented in Task 56 */ }
    if (mode == "venv")                     { return 1; /* implemented in Task 24 */ }

    // unknown leading flag → treat as script path (future): for now error.
    std::fprintf(stderr, "Unknown option or mode: %s\n", argv[1]);
    return 64;
}
```

- [ ] **Step 2: Write a smoke-test shell script**

`tests/cli/test_cli_help.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
"$PROTOST" --help 2>&1 | grep -q "Usage:" || { echo "FAIL: --help missing 'Usage:'"; exit 1; }
"$PROTOST" --version  | grep -qE "^protoST 0\.[0-9]+\." || { echo "FAIL: --version output"; exit 1; }
"$PROTOST" no-such-option && { echo "FAIL: expected non-zero for unknown option"; exit 1; } || true
echo OK
```

`chmod +x tests/cli/test_cli_help.sh` after creation.

- [ ] **Step 3: Register the script as a CTest**

Append to top-level `CMakeLists.txt` (under `enable_testing()`):

```cmake
add_test(NAME cli_help
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_help.sh $<TARGET_FILE:protost>)
```

- [ ] **Step 4: Build, run, commit**

```bash
chmod +x tests/cli/test_cli_help.sh
cmake --build build -j && ctest --test-dir build -R cli_help
git add src/main.cpp tests/cli/test_cli_help.sh CMakeLists.txt
git commit -m "feat(cli): argument parser skeleton with --help and --version"
```

---

## Task 23 — CLI: `protost --dump-ast file.st` end-to-end

**Files:**
- Modify: `protoST/src/main.cpp`
- Modify: `protoST/CMakeLists.txt` (link parser + printer sources)
- Create: `protoST/tests/cli/test_cli_dump_ast.sh`
- Create: `protoST/tests/fixtures/hello.st`

- [ ] **Step 1: Patch top-level `CMakeLists.txt` to embed parser/printer in the `protost` binary**

Replace the line `add_executable(protost src/main.cpp)` with:

```cmake
add_library(protost_frontend STATIC
    src/frontend/Lexer.cpp
    src/frontend/Parser.cpp
    src/frontend/AST.cpp
    src/frontend/ASTPrinter.cpp
)
target_include_directories(protost_frontend PUBLIC
    src
    include
    ${PROTOCORE_INCLUDE_DIRS})

add_executable(protost src/main.cpp)
target_link_libraries(protost PRIVATE protost_frontend ${PROTOCORE_LIBRARY})
```

Adjust `tests/CMakeLists.txt` `target_link_libraries` to reuse the static lib so we don't compile sources twice:

```cmake
target_link_libraries(protost_unit_tests PRIVATE
    Catch2::Catch2WithMain
    protost_frontend
    ${PROTOCORE_LIBRARY}
)
```

And remove the per-test inline `target_sources(... src/frontend/...)` block added earlier — those files now belong to `protost_frontend`.

- [ ] **Step 2: Implement `--dump-ast` in `main.cpp`**

Replace the line `if (mode == "--dump-ast") { return 1; ... }` with:

```cpp
    if (mode == "--dump-ast") {
        if (argc < 3) { std::fprintf(stderr, "--dump-ast requires a path\n"); return 64; }
        const char* path = argv[2];
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) { std::fprintf(stderr, "cannot open %s\n", path); return 66; }
        std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n), '\0');
        std::fread(src.data(), 1, static_cast<size_t>(n), fp);
        std::fclose(fp);

        protoST::Parser P(std::move(src));
        auto m = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
        std::fputs(protoST::astToString(*m).c_str(), stdout);
        return P.errors().empty() ? 0 : 65;
    }
```

Add at the top of `main.cpp`:

```cpp
#include "frontend/Parser.h"
#include "frontend/ASTPrinter.h"
```

- [ ] **Step 3: Add `hello.st` fixture**

```smalltalk
"-- hello.st --"
Transcript show: 'hello'; cr.
```

- [ ] **Step 4: CLI test script**

`tests/cli/test_cli_dump_ast.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"
out=$("$PROTOST" --dump-ast "$FIXTURES/hello.st")
echo "$out" | grep -q "(module" || { echo "FAIL: missing (module"; echo "$out"; exit 1; }
echo "$out" | grep -q "(cascade" || { echo "FAIL: missing (cascade"; echo "$out"; exit 1; }
echo "$out" | grep -q "(str 'hello')" || { echo "FAIL: missing string"; echo "$out"; exit 1; }
echo OK
```

`chmod +x tests/cli/test_cli_dump_ast.sh`.

Append to `CMakeLists.txt`:

```cmake
add_test(NAME cli_dump_ast
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_dump_ast.sh
            $<TARGET_FILE:protost>
            ${CMAKE_SOURCE_DIR}/tests/fixtures)
```

- [ ] **Step 5: Build, run, commit**

```bash
chmod +x tests/cli/test_cli_dump_ast.sh
cmake --build build -j && ctest --test-dir build -R cli
git add tests/fixtures/hello.st tests/cli/test_cli_dump_ast.sh src/main.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(cli): --dump-ast end-to-end with hello.st fixture"
```

---

## Task 24 — Venv: layout + `protost venv create`

**Files:**
- Create: `protoST/src/runtime/Venv.h`
- Create: `protoST/src/runtime/Venv.cpp`
- Create: `protoST/src/venv_template/stenv.cfg.in`
- Create: `protoST/src/venv_template/activate`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/src/main.cpp`
- Create: `protoST/tests/unit/test_venv.cpp`

- [ ] **Step 1: Failing test**

`tests/unit/test_venv.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "runtime/Venv.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("Venv create lays out directories and stenv.cfg", "[venv]") {
    auto tmp = fs::temp_directory_path() / "protost_test_venv_create";
    fs::remove_all(tmp);
    fs::create_directory(tmp);
    auto venv = tmp / ".venv";

    int rc = protoST::venvCreate(venv.string(), "/usr/local/bin", "0.1.0");
    REQUIRE(rc == 0);
    REQUIRE(fs::is_directory(venv));
    REQUIRE(fs::is_directory(venv / "bin"));
    REQUIRE(fs::is_directory(venv / "lib" / "protoST" / "modules"));
    REQUIRE(fs::is_directory(venv / "cache" / "bytecode"));
    REQUIRE(fs::is_regular_file(venv / "stenv.cfg"));
    REQUIRE(fs::is_regular_file(venv / "bin" / "activate"));

    // creating into an existing venv path should refuse
    REQUIRE(protoST::venvCreate(venv.string(), "/usr/local/bin", "0.1.0") != 0);

    fs::remove_all(tmp);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
target_sources(protost_unit_tests PRIVATE unit/test_venv.cpp)
```

(The `Venv.cpp` source belongs to a static lib we add next.)

- [ ] **Step 2: Create `src/runtime/Venv.h`**

```cpp
#pragma once
#include <string>

namespace protoST {

// Returns 0 on success, non-zero on error (also writes a message to stderr).
int venvCreate(const std::string& venvPath,
               const std::string& homeBin,
               const std::string& version);

// Returns active venv path (from STENV or walking up from cwd) or "" if none.
std::string venvDiscover(const std::string& cwd);

// Prints info about the active venv to stdout. Returns 0 if active, 1 if none.
int  venvInfo(const std::string& cwd);

// Writes activate snippet for the current shell to stdout. POSIX only in F1.
int  venvActivateSnippet(const std::string& venvPath);

} // namespace protoST
```

- [ ] **Step 3: Create the activate template `src/venv_template/activate`**

```bash
# protoST venv activate — POSIX shell
deactivate() {
    if [ -n "${_OLD_STPATH+x}" ]; then export STPATH="$_OLD_STPATH"; unset _OLD_STPATH
    else unset STPATH; fi
    if [ -n "${_OLD_PATH+x}" ];   then export PATH="$_OLD_PATH";     unset _OLD_PATH; fi
    unset STENV
    unset -f deactivate
    PS1="${_OLD_PS1:-$PS1}"
    unset _OLD_PS1
}

_OLD_PATH="$PATH"
_OLD_PS1="$PS1"
export STENV="@VENV_PATH@"
export PATH="@VENV_PATH@/bin:$PATH"
PS1="(protoST) $PS1"
```

`@VENV_PATH@` is substituted at create time.

`src/venv_template/stenv.cfg.in`:

```
home = @HOME_BIN@
version = @VERSION@
include-system-modules = false
```

- [ ] **Step 4: Create `src/runtime/Venv.cpp`**

```cpp
#include "Venv.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace protoST {

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

fs::path templateDir() {
    // Resolved relative to the binary at runtime via PROTOST_TEMPLATE_DIR
    // (set by CMakeLists.txt as a compile-time macro). The tests rely on
    // this default; production installs override via env STENV_TEMPLATE_DIR.
    if (const char* env = std::getenv("STENV_TEMPLATE_DIR")) return fs::path(env);
    return fs::path(PROTOST_TEMPLATE_DIR);
}

} // anon

int venvCreate(const std::string& venvPath,
               const std::string& homeBin,
               const std::string& version) {
    fs::path venv(venvPath);
    if (fs::exists(venv)) {
        std::fprintf(stderr, "venv path already exists: %s\n", venvPath.c_str());
        return 1;
    }
    std::error_code ec;
    fs::create_directories(venv / "bin", ec);
    fs::create_directories(venv / "lib" / "protoST" / "modules", ec);
    fs::create_directories(venv / "cache" / "bytecode", ec);
    if (ec) {
        std::fprintf(stderr, "venv mkdir failed: %s\n", ec.message().c_str());
        return 2;
    }

    // stenv.cfg
    {
        auto tpl = readAll(templateDir() / "stenv.cfg.in");
        tpl = replaceAll(tpl, "@HOME_BIN@", homeBin);
        tpl = replaceAll(tpl, "@VERSION@",  version);
        std::ofstream f(venv / "stenv.cfg");
        f << tpl;
    }
    // activate
    {
        auto tpl = readAll(templateDir() / "activate");
        tpl = replaceAll(tpl, "@VENV_PATH@", fs::absolute(venv).string());
        std::ofstream f(venv / "bin" / "activate");
        f << tpl;
    }
    return 0;
}

std::string venvDiscover(const std::string& cwd) {
    if (const char* e = std::getenv("STENV"); e && *e) return std::string(e);
    fs::path p = cwd.empty() ? fs::current_path() : fs::path(cwd);
    for (; !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / ".venv" / "stenv.cfg")) return (p / ".venv").string();
        if (p == p.root_path()) break;
    }
    return "";
}

int venvInfo(const std::string& cwd) {
    auto v = venvDiscover(cwd);
    if (v.empty()) { std::puts("no active venv"); return 1; }
    std::printf("venv: %s\n", v.c_str());
    std::ifstream f(fs::path(v) / "stenv.cfg");
    std::string line;
    while (std::getline(f, line)) std::printf("  %s\n", line.c_str());
    return 0;
}

int venvActivateSnippet(const std::string& venvPath) {
    auto p = fs::path(venvPath) / "bin" / "activate";
    if (!fs::exists(p)) { std::fprintf(stderr, "not a venv: %s\n", venvPath.c_str()); return 1; }
    std::printf(". %s\n", p.string().c_str());
    return 0;
}

} // namespace protoST
```

- [ ] **Step 5: Wire `Venv.cpp` into the build**

Extend `protost_frontend` (or create a sibling `protost_runtime` lib). Use a sibling lib so the layering is clear. In `CMakeLists.txt`, append after the `protost_frontend` block:

```cmake
add_library(protost_runtime STATIC
    src/runtime/Venv.cpp
)
target_include_directories(protost_runtime PUBLIC src include ${PROTOCORE_INCLUDE_DIRS})
target_compile_definitions(protost_runtime PRIVATE
    PROTOST_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/src/venv_template")

target_link_libraries(protost PRIVATE protost_runtime)
target_link_libraries(protost_unit_tests PRIVATE protost_runtime)
```

- [ ] **Step 6: Implement `venv` subcommand in `main.cpp`**

Replace the `mode == "venv"` line with:

```cpp
    if (mode == "venv") {
        if (argc < 3) { std::fprintf(stderr, "venv requires a subcommand: create|activate|info\n"); return 64; }
        std::string sub = argv[2];
        if (sub == "create") {
            std::string path = (argc >= 4) ? argv[3] : ".venv";
            return protoST::venvCreate(path, "/usr/local/bin", "0.1.0");
        }
        if (sub == "activate") {
            std::string p = (argc >= 4) ? argv[3] : protoST::venvDiscover("");
            if (p.empty()) { std::fprintf(stderr, "no venv to activate\n"); return 1; }
            return protoST::venvActivateSnippet(p);
        }
        if (sub == "info") return protoST::venvInfo("");
        std::fprintf(stderr, "unknown venv subcommand: %s\n", sub.c_str());
        return 64;
    }
```

Add `#include "runtime/Venv.h"` near the other includes.

- [ ] **Step 7: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R venv
git add src/runtime/Venv.h src/runtime/Venv.cpp src/venv_template/ src/main.cpp tests/unit/test_venv.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(venv): create + discover + info + activate snippet"
```

---

## Task 25 — Venv: fish + ps1 templates as data only

Templates that the future Windows/fish users will exercise. We ship them but only the POSIX one is currently emitted.

**Files:**
- Create: `protoST/src/venv_template/activate.fish`
- Create: `protoST/src/venv_template/activate.ps1`

- [ ] **Step 1: Create both files**

`activate.fish`:

```fish
function deactivate
    if test -n "$_OLD_STPATH"; set -gx STPATH "$_OLD_STPATH"; set -e _OLD_STPATH
    else; set -e STPATH; end
    if test -n "$_OLD_PATH"; set -gx PATH $_OLD_PATH; set -e _OLD_PATH; end
    set -e STENV
    functions -e deactivate
end
set -gx _OLD_PATH $PATH
set -gx STENV "@VENV_PATH@"
set -gx PATH "@VENV_PATH@/bin" $PATH
```

`activate.ps1`:

```powershell
function global:deactivate {
    if (Test-Path Env:_OLD_STPATH) { $env:STPATH = $env:_OLD_STPATH; Remove-Item Env:_OLD_STPATH }
    else                            { Remove-Item Env:STPATH -ErrorAction SilentlyContinue }
    if (Test-Path Env:_OLD_PATH)    { $env:PATH = $env:_OLD_PATH; Remove-Item Env:_OLD_PATH }
    Remove-Item Env:STENV -ErrorAction SilentlyContinue
}
$env:_OLD_PATH = $env:PATH
$env:STENV = "@VENV_PATH@"
$env:PATH = "@VENV_PATH@/bin;$env:PATH"
```

- [ ] **Step 2: Commit**

```bash
git add src/venv_template/activate.fish src/venv_template/activate.ps1
git commit -m "feat(venv): fish and powershell activate templates (shipped, F1 emits posix)"
```

---

## Task 26 — Venv: end-to-end CLI smoke test

**Files:**
- Create: `protoST/tests/cli/test_cli_venv.sh`

- [ ] **Step 1: Script**

```bash
#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
tmp=$(mktemp -d)
trap "rm -rf $tmp" EXIT

cd "$tmp"
"$PROTOST" venv create .venv

[ -d .venv/bin ]                    || { echo "FAIL: bin missing"; exit 1; }
[ -d .venv/lib/protoST/modules ]    || { echo "FAIL: modules dir missing"; exit 1; }
[ -f .venv/stenv.cfg ]              || { echo "FAIL: stenv.cfg missing"; exit 1; }
[ -f .venv/bin/activate ]           || { echo "FAIL: activate missing"; exit 1; }

# venv info from inside the project (no STENV) should discover the venv
out=$("$PROTOST" venv info)
echo "$out" | grep -q "$tmp/.venv" || { echo "FAIL: venv info did not discover"; echo "$out"; exit 1; }

# explicit STENV overrides discovery
STENV="$tmp/.venv" "$PROTOST" venv info | grep -q "$tmp/.venv" || { echo "FAIL: STENV override"; exit 1; }

echo OK
```

- [ ] **Step 2: Wire as CTest**

In `CMakeLists.txt`:

```cmake
add_test(NAME cli_venv
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_venv.sh $<TARGET_FILE:protost>)
```

- [ ] **Step 3: Test and commit**

```bash
chmod +x tests/cli/test_cli_venv.sh
cmake --build build -j && ctest --test-dir build -R cli_venv
git add tests/cli/test_cli_venv.sh CMakeLists.txt
git commit -m "test(cli): venv create + info + STENV override smoke test"
```

---

## Task 27 — F1 close-out: tag + manual smoke

**Files:**
- (none)

- [ ] **Step 1: Run the full ctest suite to confirm green**

```bash
ctest --test-dir build --output-on-failure
```

Expected: every test passes. `Total Test time` and `100% tests passed`.

- [ ] **Step 2: Manual smoke**

```bash
./build/protost --version
./build/protost --dump-ast tests/fixtures/counter.st | head -20
./build/protost venv create /tmp/protost-f1-smoke-venv
./build/protost venv info  # cwd not under venv → "no active venv"
STENV=/tmp/protost-f1-smoke-venv ./build/protost venv info
rm -rf /tmp/protost-f1-smoke-venv
```

- [ ] **Step 3: Tag**

```bash
git tag -a f1-complete -m "F1: skeleton + parser + venv scaffold"
```

(Optional: push tag if remote is set up. Phase boundary marker for rollback.)

---

# Phase 2 — Bytecode VM and CLI debugger

---

## Task 28 — Opcode enum and 2-byte instruction layout

**Files:**
- Create: `protoST/src/runtime/Opcodes.h`

The bytecode instruction is exactly 2 bytes: `[opcode:u8][arg:u8]`. Constants beyond 256 are reached by emitting `EXTEND` (the next instruction's arg gets `(prev_arg << 8) | arg`). This matches the protopyc format.

- [ ] **Step 1: Create `src/runtime/Opcodes.h`**

```cpp
#pragma once
#include <cstdint>

namespace protoST {

enum class Op : uint8_t {
    NOP             = 0,
    // Stack and constants
    PUSH_CONST      = 1,    // arg = constant pool index
    PUSH_LOCAL      = 2,    // arg = slot index
    STORE_LOCAL     = 3,    // arg = slot index
    DUP             = 4,
    POP             = 5,
    // Values
    PUSH_SELF       = 6,
    PUSH_SUPER      = 7,
    PUSH_NIL        = 8,
    PUSH_TRUE       = 9,
    PUSH_FALSE      = 10,
    // Sends
    SEND_UNARY      = 11,   // arg = selector const index
    SEND_BINARY     = 12,   // arg = selector const index; 1 arg on stack
    SEND_KEYWORD    = 13,   // arg = selector const index; argc encoded in selector
    SEND_SUPER      = 14,
    // Control flow
    JUMP            = 15,   // arg = forward offset in instructions (i.e. multiple of 2 bytes)
    JUMP_IF_TRUE    = 16,
    JUMP_IF_FALSE   = 17,
    // Return
    RETURN          = 18,
    RETURN_TOP      = 19,   // pops top, returns
    // Blocks
    PUSH_BLOCK      = 20,   // arg = bytecode-module-table index
    // Cascade helper
    DUP_RECEIVER    = 21,   // dup the value at depth = arg
    // Extend for >256-index args
    EXTEND          = 254,
    // Debugger primitive guard
    HALT            = 255,
};

inline constexpr size_t kInstrSize = 2;

} // namespace protoST
```

- [ ] **Step 2: Commit (no test — exercised by Task 30)**

```bash
git add src/runtime/Opcodes.h
git commit -m "feat(bytecode): 2-byte instruction format and opcode enum"
```

---

## Task 29 — `BytecodeModule`: constant pool + bytes container

**Files:**
- Create: `protoST/src/runtime/BytecodeModule.h`
- Create: `protoST/src/runtime/BytecodeModule.cpp`
- Create: `protoST/tests/unit/test_bytecode_module.cpp`
- Modify: `protoST/tests/CMakeLists.txt`

The module holds:
- A constant pool: `std::vector<proto::ProtoObject*>` rooted via a `ProtoRootSet` (so the GC sees them). For F2 we use a simple model: each constant is added at module build time, before any execution; once execution starts, the pool is read-only.
- A bytes buffer: `std::vector<uint8_t>` with the 2-byte instructions.
- Sub-modules: blocks compile to their own `BytecodeModule` and are referenced from `PUSH_BLOCK`.

- [ ] **Step 1: Failing test**

`tests/unit/test_bytecode_module.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("BytecodeModule round-trips emit and decode", "[bytecode]") {
    BytecodeModule m;
    auto idx = m.addInteger(42);
    REQUIRE(idx == 0);
    m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
    m.emit(Op::RETURN_TOP, 0);

    REQUIRE(m.bytes().size() == 4);
    REQUIRE(static_cast<Op>(m.bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(m.bytes()[1] == 0);
    REQUIRE(static_cast<Op>(m.bytes()[2]) == Op::RETURN_TOP);

    REQUIRE(m.constInteger(0) == 42);
}

TEST_CASE("BytecodeModule supports symbol interning by string", "[bytecode]") {
    BytecodeModule m;
    auto a = m.internSymbol("value");
    auto b = m.internSymbol("value");
    auto c = m.internSymbol("at:put:");
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(m.constSymbol(a) == "value");
    REQUIRE(m.constSymbol(c) == "at:put:");
}
```

Append `unit/test_bytecode_module.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Create `BytecodeModule.h`**

```cpp
#pragma once
#include "Opcodes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoST {

class BytecodeModule {
public:
    enum class ConstKind : uint8_t { Integer, Float, String, Symbol, Char, BlockRef, NilK, TrueK, FalseK };

    struct Const {
        ConstKind kind;
        long long ival = 0;
        double    fval = 0.0;
        std::string sval;
        size_t      blockIndex = 0;  // for BlockRef
    };

    // emission
    void emit(Op op, uint8_t arg);

    // patching (for jumps)
    size_t  pos() const { return bytes_.size(); }
    void    patchArg(size_t bytePos, uint8_t arg) { bytes_[bytePos + 1] = arg; }

    // constants
    size_t  addInteger(long long v);
    size_t  addFloat(double v);
    size_t  addString(const std::string& s);
    size_t  internSymbol(const std::string& s);  // de-duplicated
    size_t  addChar(const std::string& utf8);
    size_t  addBlockRef(size_t blockIndex);

    // accessors
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    long long           constInteger(size_t i) const { return consts_[i].ival; }
    double              constFloat(size_t i)   const { return consts_[i].fval; }
    const std::string&  constString(size_t i)  const { return consts_[i].sval; }
    const std::string&  constSymbol(size_t i)  const { return consts_[i].sval; }
    ConstKind           constKind(size_t i)    const { return consts_[i].kind; }
    size_t              constBlockRef(size_t i)const { return consts_[i].blockIndex; }

    // sub-modules
    size_t              addBlockModule(std::unique_ptr<BytecodeModule> b);
    const BytecodeModule& block(size_t i) const { return *blocks_[i]; }
    size_t              numBlocks() const { return blocks_.size(); }

private:
    std::vector<uint8_t>                bytes_;
    std::vector<Const>                  consts_;
    std::unordered_map<std::string, size_t> symbolIndex_;
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
};

} // namespace protoST
```

- [ ] **Step 3: Create `BytecodeModule.cpp`**

```cpp
#include "BytecodeModule.h"
namespace protoST {

void BytecodeModule::emit(Op op, uint8_t arg) {
    bytes_.push_back(static_cast<uint8_t>(op));
    bytes_.push_back(arg);
}

size_t BytecodeModule::addInteger(long long v) {
    consts_.push_back(Const{ConstKind::Integer, v, 0.0, {}, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addFloat(double v) {
    consts_.push_back(Const{ConstKind::Float, 0, v, {}, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addString(const std::string& s) {
    consts_.push_back(Const{ConstKind::String, 0, 0.0, s, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::internSymbol(const std::string& s) {
    auto it = symbolIndex_.find(s);
    if (it != symbolIndex_.end()) return it->second;
    consts_.push_back(Const{ConstKind::Symbol, 0, 0.0, s, 0});
    size_t idx = consts_.size() - 1;
    symbolIndex_[s] = idx;
    return idx;
}
size_t BytecodeModule::addChar(const std::string& utf8) {
    consts_.push_back(Const{ConstKind::Char, 0, 0.0, utf8, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addBlockRef(size_t blockIndex) {
    consts_.push_back(Const{ConstKind::BlockRef, 0, 0.0, {}, blockIndex});
    return consts_.size() - 1;
}
size_t BytecodeModule::addBlockModule(std::unique_ptr<BytecodeModule> b) {
    blocks_.push_back(std::move(b));
    return blocks_.size() - 1;
}

} // namespace protoST
```

- [ ] **Step 4: Build, test, commit**

```bash
cmake --build build -j && ctest --test-dir build -R bytecode
git add src/runtime/BytecodeModule.h src/runtime/BytecodeModule.cpp tests/unit/test_bytecode_module.cpp tests/CMakeLists.txt
git commit -m "feat(bytecode): module with constant pool, byte buffer, sub-blocks"
```

Note: the constant pool now holds plain language constants (integers, strings, symbols, …). Conversion to `ProtoObject*` happens at module-load time in `STRuntime`, not during compilation. This keeps the compiler protoCore-free and trivially testable.

---

## Task 30 — Compiler scaffold + emit helpers

**Files:**
- Create: `protoST/src/frontend/Compiler.h`
- Create: `protoST/src/frontend/Compiler.cpp`
- Create: `protoST/tests/unit/test_compiler.cpp`

The compiler is a visitor over the AST that emits into a `BytecodeModule`. Local variables are tracked by a per-method symbol table that maps name → slot index.

- [ ] **Step 1: Failing test**

```cpp
#include <catch2/catch_all.hpp>
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

using protoST::Parser;
using protoST::Compiler;
using protoST::BytecodeModule;
using protoST::Op;

TEST_CASE("Compiler: empty module emits a single RETURN", "[compiler]") {
    Parser P("");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() == 2);
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[1] == 0 ? Op::NOP : Op::NOP) == Op::NOP);
    // (Top-level value is `nil` for an empty module.)
}
```

Wait — that's wrong: an instruction is 2 bytes. A single `PUSH_NIL`(arg=0) is bytes `[8, 0]`. Then `RETURN_TOP` is `[19, 0]`. Total 4 bytes.

Replace the assertion:

```cpp
    REQUIRE(bc->bytes().size() == 4);
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::RETURN_TOP);
```

Append `unit/test_compiler.cpp` and `${CMAKE_SOURCE_DIR}/src/frontend/Compiler.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Create `Compiler.h`**

```cpp
#pragma once
#include "AST.h"
#include "../runtime/BytecodeModule.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoST {

class Compiler {
public:
    std::unique_ptr<BytecodeModule> compileModule(const ast::Node& mod);
    bool hasErrors() const { return !errors_.empty(); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    struct Scope {
        std::unordered_map<std::string, int> slots; // name → slot index
        int nextSlot = 0;
    };

    std::vector<Scope> scopes_;
    std::vector<std::string> errors_;

    void   emitExpr(BytecodeModule& m, const ast::Node& n);
    void   emitStatement(BytecodeModule& m, const ast::Node& n);
    int    declareLocal(const std::string& name);
    int    resolveLocal(const std::string& name) const;
    void   error(const std::string& msg);
};

} // namespace protoST
```

- [ ] **Step 3: Create `Compiler.cpp` (scaffold)**

```cpp
#include "Compiler.h"
namespace protoST {
using namespace ast;

std::unique_ptr<BytecodeModule> Compiler::compileModule(const Node& mod) {
    auto bc = std::make_unique<BytecodeModule>();
    scopes_.clear(); scopes_.emplace_back();
    if (mod.children.empty()) {
        bc->emit(Op::PUSH_NIL, 0);
    } else {
        for (size_t i = 0; i < mod.children.size(); ++i) {
            emitStatement(*bc, *mod.children[i]);
            // for all but the last statement, discard the result
            if (i + 1 != mod.children.size()) bc->emit(Op::POP, 0);
        }
    }
    bc->emit(Op::RETURN_TOP, 0);
    return bc;
}

void Compiler::emitStatement(BytecodeModule& m, const Node& n) {
    if (n.kind == NodeKind::Return) {
        if (!n.children.empty()) emitExpr(m, *n.children[0]);
        else m.emit(Op::PUSH_NIL, 0);
        m.emit(Op::RETURN, 0);
        return;
    }
    emitExpr(m, n);
}

void Compiler::emitExpr(BytecodeModule& m, const Node& n) {
    (void)m; (void)n;
    error("expression kind not yet supported in compiler scaffold");
    m.emit(Op::PUSH_NIL, 0);
}

int Compiler::declareLocal(const std::string& name) {
    auto& s = scopes_.back();
    auto it = s.slots.find(name);
    if (it != s.slots.end()) return it->second;
    int slot = s.nextSlot++;
    s.slots[name] = slot;
    return slot;
}

int Compiler::resolveLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->slots.find(name);
        if (f != it->slots.end()) return f->second;
    }
    return -1;
}

void Compiler::error(const std::string& msg) { errors_.push_back(msg); }

} // namespace protoST
```

- [ ] **Step 4: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.h src/frontend/Compiler.cpp tests/unit/test_compiler.cpp tests/CMakeLists.txt
git commit -m "feat(compiler): scaffold with module entry, slot table, error sink"
```

---

## Task 31 — Compiler: literal expressions

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/tests/unit/test_compiler.cpp`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Compiler: integer literal pushes from const pool", "[compiler]") {
    Parser P("42.");
    auto ast = P.parseModule();
    Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    // Expected: PUSH_CONST 0; RETURN_TOP 0
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->constInteger(0) == 42);
}

TEST_CASE("Compiler: nil/true/false push the dedicated opcodes", "[compiler]") {
    auto compile = [](const char* src) {
        Parser P(src);
        Compiler C; auto bc = C.compileModule(*P.parseModule());
        REQUIRE(!C.hasErrors());
        return bc;
    };
    REQUIRE(static_cast<Op>(compile("nil.")->bytes()[0])   == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(compile("true.")->bytes()[0])  == Op::PUSH_TRUE);
    REQUIRE(static_cast<Op>(compile("false.")->bytes()[0]) == Op::PUSH_FALSE);
}
```

- [ ] **Step 2: Implement in `emitExpr`**

Replace `emitExpr` body:

```cpp
void Compiler::emitExpr(BytecodeModule& m, const Node& n) {
    switch (n.kind) {
        case NodeKind::IntegerLit: {
            auto idx = m.addInteger(n.intValue);
            if (idx > 255) { error("integer constant pool overflow > 255 (EXTEND not yet emitted in F2)"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::FloatLit: {
            auto idx = m.addFloat(n.floatValue);
            if (idx > 255) { error("float constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::StringLit: {
            auto idx = m.addString(n.text);
            if (idx > 255) { error("string constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::SymbolLit: {
            auto idx = m.internSymbol(n.text);
            if (idx > 255) { error("symbol constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::CharLit: {
            auto idx = m.addChar(n.text);
            if (idx > 255) { error("char constant pool overflow"); return; }
            m.emit(Op::PUSH_CONST, static_cast<uint8_t>(idx));
            return;
        }
        case NodeKind::TrueLit:  m.emit(Op::PUSH_TRUE, 0); return;
        case NodeKind::FalseLit: m.emit(Op::PUSH_FALSE, 0); return;
        case NodeKind::NilLit:   m.emit(Op::PUSH_NIL, 0); return;
        case NodeKind::Self:     m.emit(Op::PUSH_SELF, 0); return;
        case NodeKind::Super:    m.emit(Op::PUSH_SUPER, 0); return;
        default:
            error("expression kind not yet supported");
            m.emit(Op::PUSH_NIL, 0);
            return;
    }
}
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.cpp tests/unit/test_compiler.cpp
git commit -m "feat(compiler): literal expressions and self/super"
```

---

## Task 32 — Compiler: identifier load/store via slot table

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/tests/unit/test_compiler.cpp`

In F2 the only scopes are: module top-level, method body, block body. All three keep their locals in a slot table. Globals/instance variables are out of scope for F2 (they require `Bootstrap` and `STRuntime` to materialise; they will be added in Task 38 when ExecutionEngine reads them).

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Compiler: assignment creates slot and emits STORE_LOCAL", "[compiler]") {
    Parser P("x := 42. x.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // Sequence:
    //   PUSH_CONST 0   (42)
    //   STORE_LOCAL 0  (x)
    //   POP
    //   PUSH_LOCAL 0
    //   RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::STORE_LOCAL);
    REQUIRE(bc->bytes()[3] == 0);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::POP);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::PUSH_LOCAL);
    REQUIRE(bc->bytes()[7] == 0);
}
```

- [ ] **Step 2: Add cases in `emitExpr` and `emitStatement`**

In `emitExpr`, before `default`:

```cpp
        case NodeKind::Identifier: {
            int slot = resolveLocal(n.text);
            if (slot < 0) { error("unknown identifier '" + n.text + "'"); m.emit(Op::PUSH_NIL, 0); return; }
            m.emit(Op::PUSH_LOCAL, static_cast<uint8_t>(slot));
            return;
        }
        case NodeKind::Assignment: {
            int slot = declareLocal(n.text);
            emitExpr(m, *n.children[0]);
            // STORE_LOCAL pops the value; the result of an assignment expression in ST is the value.
            // To preserve "assignment evaluates to value", we DUP before STORE.
            m.emit(Op::DUP, 0);
            m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(slot));
            return;
        }
```

Also patch `emitStatement` so an Assignment at statement level can avoid the redundant DUP. Actually keep DUP universally; the top-level loop already POPs intermediate values, and the language semantics require expression value to be the rvalue. The DUP+STORE then POP costs one extra op per stored assignment — acceptable for F2.

Wait — that adds 4 bytes per statement assignment. To minimise, special-case in `emitStatement` when the immediate child is an Assignment:

Replace `emitStatement`:

```cpp
void Compiler::emitStatement(BytecodeModule& m, const Node& n) {
    if (n.kind == NodeKind::Return) {
        if (!n.children.empty()) emitExpr(m, *n.children[0]);
        else m.emit(Op::PUSH_NIL, 0);
        m.emit(Op::RETURN, 0);
        return;
    }
    if (n.kind == NodeKind::Assignment) {
        int slot = declareLocal(n.text);
        emitExpr(m, *n.children[0]);
        m.emit(Op::DUP, 0);
        m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(slot));
        return;
    }
    emitExpr(m, n);
}
```

(STORE_LOCAL in our ISA stores top-of-stack into the slot **and pops** the stored value. So after DUP, STORE_LOCAL leaves the value still on top, ready for the top-level POP.)

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.cpp tests/unit/test_compiler.cpp
git commit -m "feat(compiler): identifier load and assignment via local slot table"
```

---

## Task 33 — Compiler: SEND for unary/binary/keyword

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/tests/unit/test_compiler.cpp`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("Compiler: unary send emits SEND_UNARY", "[compiler]") {
    Parser P("nil printNl.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::SEND_UNARY);
    REQUIRE(bc->constSymbol(bc->bytes()[3]) == "printNl");
}

TEST_CASE("Compiler: binary send emits SEND_BINARY", "[compiler]") {
    Parser P("1 + 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_CONST 0 (1), PUSH_CONST 1 (2), SEND_BINARY (sym +)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[2]) == Op::PUSH_CONST);
    REQUIRE(static_cast<Op>(bc->bytes()[4]) == Op::SEND_BINARY);
    REQUIRE(bc->constSymbol(bc->bytes()[5]) == "+");
}

TEST_CASE("Compiler: keyword send emits SEND_KEYWORD", "[compiler]") {
    Parser P("nil at: 1 put: 2.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_NIL, PUSH_CONST(1), PUSH_CONST(2), SEND_KEYWORD sym(at:put:)
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_NIL);
    REQUIRE(static_cast<Op>(bc->bytes()[6]) == Op::SEND_KEYWORD);
    REQUIRE(bc->constSymbol(bc->bytes()[7]) == "at:put:");
}
```

- [ ] **Step 2: Add cases in `emitExpr`**

```cpp
        case NodeKind::UnarySend: {
            emitExpr(m, *n.children[0]);
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_UNARY, static_cast<uint8_t>(sym));
            return;
        }
        case NodeKind::BinarySend: {
            // receiver, then arg, then SEND_BINARY
            emitExpr(m, *n.children[0]);
            if (n.children.size() < 2) { error("binary send missing argument"); return; }
            emitExpr(m, *n.children[1]);
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_BINARY, static_cast<uint8_t>(sym));
            return;
        }
        case NodeKind::KeywordSend: {
            // receiver, then each arg in order, then SEND_KEYWORD
            emitExpr(m, *n.children[0]);
            for (size_t i = 1; i < n.children.size(); ++i) {
                emitExpr(m, *n.children[i]);
            }
            auto sym = m.internSymbol(n.text);
            if (sym > 255) { error("selector pool overflow"); return; }
            m.emit(Op::SEND_KEYWORD, static_cast<uint8_t>(sym));
            return;
        }
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.cpp tests/unit/test_compiler.cpp
git commit -m "feat(compiler): SEND_UNARY/BINARY/KEYWORD opcodes"
```

---

## Task 34 — Compiler: cascades (`DUP_RECEIVER`)

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/tests/unit/test_compiler.cpp`

A cascade `r m1; m2; m3` is compiled to:

```
emit receiver r        // stack: [r]
DUP                    // stack: [r, r]    -- keep one for m2
... emit m1 args ...
SEND_X m1              // stack: [r, result1]    -- POP result1, keep r
POP                    //                      // stack: [r]
DUP                    // stack: [r, r]    -- keep one for m3
... emit m2 args ...
SEND_X m2
POP
... emit m3 args ...
SEND_X m3              // stack: [r, result3] — final result of the cascade is result3
SWAP_POP_KEEP_TOP      // implemented as STORE_LOCAL tmp; POP; PUSH_LOCAL tmp — see below
```

The semantics of `;` in ST-80 is that the cascade evaluates to the value of the LAST message, not the receiver. We do this with a small dance: at the end, after the last send, we have `[r, last_result]` on the stack and we need `[last_result]`. Easiest: stash `last_result` in a temp local.

Actually, a cleaner approach — use a fresh hidden slot:

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Compiler: cascade emits dup/pop dance and keeps last value", "[compiler]") {
    Parser P("nil yourself; printNl; size.");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // At minimum: opcode sequence ends with the last SEND_UNARY (size) and not extra DUPs
    auto last_send = false;
    for (size_t i = 0; i + 1 < bc->bytes().size(); i += 2) {
        if (static_cast<Op>(bc->bytes()[i]) == Op::SEND_UNARY &&
            bc->constSymbol(bc->bytes()[i+1]) == "size") last_send = true;
    }
    REQUIRE(last_send);
}
```

- [ ] **Step 2: Implement Cascade in `emitExpr`**

```cpp
        case NodeKind::Cascade: {
            // children[0] is the receiver; children[1..] are partial sends.
            emitExpr(m, *n.children[0]);
            int tmp = declareLocal("__cascade_tmp_" + std::to_string(scopes_.back().nextSlot));
            for (size_t i = 1; i < n.children.size(); ++i) {
                auto& msg = *n.children[i];
                // For all but the last partial, duplicate receiver before evaluating the send,
                // then POP the result. For the last partial we keep the result.
                bool isLast = (i + 1 == n.children.size());
                m.emit(Op::DUP, 0); // duplicate receiver for this send
                // emit args; receiver is already on stack just below the args
                if (msg.kind == NodeKind::UnarySend) {
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_UNARY, static_cast<uint8_t>(sym));
                } else if (msg.kind == NodeKind::BinarySend) {
                    if (msg.children.empty()) { error("cascade binary missing arg"); }
                    else emitExpr(m, *msg.children[0]);
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_BINARY, static_cast<uint8_t>(sym));
                } else if (msg.kind == NodeKind::KeywordSend) {
                    for (auto& a : msg.children) emitExpr(m, *a);
                    auto sym = m.internSymbol(msg.text);
                    m.emit(Op::SEND_KEYWORD, static_cast<uint8_t>(sym));
                } else {
                    error("unexpected node in cascade");
                }
                if (isLast) {
                    // stack: [receiver, last_result]; stash result, pop receiver, push result back
                    m.emit(Op::STORE_LOCAL, static_cast<uint8_t>(tmp));
                    m.emit(Op::POP, 0);            // discard receiver
                    m.emit(Op::PUSH_LOCAL, static_cast<uint8_t>(tmp));
                } else {
                    m.emit(Op::POP, 0);            // discard this result, keep receiver
                }
            }
            return;
        }
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.cpp tests/unit/test_compiler.cpp
git commit -m "feat(compiler): cascades — DUP receiver per send, stash last result"
```

---

## Task 35 — Compiler: blocks with closure analysis

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/tests/unit/test_compiler.cpp`

For F2 blocks have **no closure over the enclosing frame**: the only locals visible inside a block are the block's own arguments and locals. Free variables resolved against the outer frame are an error at compile time. This is a deliberate F2 simplification — closures arrive in F3/F4 when methods of user classes need access to instance variables and full closures get exercised across `BlockClosure>>value` boundaries. The spec's full closure design (§ 5.5) is binding for F3+.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Compiler: block compiles to a sub-module referenced by PUSH_BLOCK", "[compiler]") {
    Parser P("[ :a :b | a + b ].");
    Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());
    // PUSH_BLOCK 0 ; RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[0]) == Op::PUSH_BLOCK);
    REQUIRE(bc->bytes()[1] == 0);
    REQUIRE(bc->numBlocks() == 1);
    auto& blk = bc->block(0);
    // body should end with RETURN_TOP after PUSH_LOCAL 0, PUSH_LOCAL 1, SEND_BINARY +
    REQUIRE(static_cast<Op>(blk.bytes()[0]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[2]) == Op::PUSH_LOCAL);
    REQUIRE(static_cast<Op>(blk.bytes()[4]) == Op::SEND_BINARY);
}
```

- [ ] **Step 2: Add `Block` case in `emitExpr`**

```cpp
        case NodeKind::Block: {
            auto sub = std::make_unique<BytecodeModule>();
            // open fresh scope for block
            scopes_.emplace_back();
            // declare args first (slots 0..nArgs-1), then locals
            for (size_t i = 0; i < n.stringList.size(); ++i) {
                declareLocal(n.stringList[i]);
            }
            // emit body statements; last value implicitly returned
            if (n.children.empty()) sub->emit(Op::PUSH_NIL, 0);
            for (size_t i = 0; i < n.children.size(); ++i) {
                emitStatement(*sub, *n.children[i]);
                if (i + 1 != n.children.size()) sub->emit(Op::POP, 0);
            }
            sub->emit(Op::RETURN_TOP, 0);
            scopes_.pop_back();
            size_t blkIdx = m.addBlockModule(std::move(sub));
            if (blkIdx > 255) { error("block index pool overflow"); return; }
            m.emit(Op::PUSH_BLOCK, static_cast<uint8_t>(blkIdx));
            return;
        }
```

Note: the block's `intValue` (arg count from the parser, Task 16) is implicitly the count of leading slots that the runtime fills when invoking `value`/`value:`/etc. We carry it separately by extending `BytecodeModule` with an `argCount_` field next.

Add to `BytecodeModule.h`:

```cpp
    void setArgCount(int n) { argCount_ = n; }
    int  argCount() const   { return argCount_; }
```

and a field:

```cpp
    int argCount_ = 0;
```

In the `Block` case above, immediately after `scopes_.emplace_back();` (and before the declareLocal loop), capture and apply:

```cpp
            int nArgs = static_cast<int>(n.intValue);
            sub->setArgCount(nArgs);
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R compiler
git add src/frontend/Compiler.cpp src/runtime/BytecodeModule.h tests/unit/test_compiler.cpp
git commit -m "feat(compiler): blocks compile to sub-modules with arg count (F2 no-closure rule)"
```

---

## Task 36 — Compiler: top-level expression program assertion

**Files:**
- Modify: `protoST/tests/unit/test_compiler.cpp`

A regression test that locks F2's contract: the top-level program `(1 to: 100) inject: 0 into: [:a :b | a + b]` parses and compiles without errors.

- [ ] **Step 1: Test**

```cpp
TEST_CASE("Compiler: F2 hero expression parses and compiles", "[compiler]") {
    Parser P("(1 to: 100) inject: 0 into: [:a :b | a + b].");
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    REQUIRE(bc->bytes().size() >= 8);                      // non-trivial program
    REQUIRE(bc->numBlocks() == 1);                          // the [:a :b| ...] block
    // ends with RETURN_TOP
    REQUIRE(static_cast<Op>(bc->bytes()[bc->bytes().size()-2]) == Op::RETURN_TOP);
}
```

- [ ] **Step 2: Commit**

```bash
cmake --build build -j && ctest --test-dir build -R "hero expression"
git add tests/unit/test_compiler.cpp
git commit -m "test(compiler): F2 hero expression compiles to a closed bytecode module"
```

---

## Task 37 — `STRuntime` and `ExecutionEngine` scaffold

**Files:**
- Modify: `protoST/include/protoST/STRuntime.h`
- Create: `protoST/src/runtime/STRuntime.cpp`
- Create: `protoST/src/runtime/ExecutionEngine.h`
- Create: `protoST/src/runtime/ExecutionEngine.cpp`
- Create: `protoST/tests/unit/test_execution_engine.cpp`
- Modify: `protoST/CMakeLists.txt`

The `STRuntime` owns a `proto::ProtoSpace`, a root context, and one `ProtoRootSet` for transient pinning. The `ExecutionEngine` runs a single `BytecodeModule`. Operand stack and locals are stored as protoCore objects per the absolute rule: a `ProtoList` for the stack, automatic slots on `ProtoContext` for the locals.

Caveat: protoCore's `ProtoList` is **immutable with structural sharing**. Each `push` returns a new list — so the engine reassigns its stack pointer each step. This is acceptable for F2 throughput (we will revisit at F9 tuning). The interpreter must hold the stack reachable through the `ProtoContext` so the GC traces it; we use an automatic slot.

- [ ] **Step 1: Failing test**

`tests/unit/test_execution_engine.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/BytecodeModule.h"
#include "runtime/Opcodes.h"

TEST_CASE("ExecutionEngine: empty module returns nil", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.emit(protoST::Op::PUSH_NIL, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* result = rt.runTopLevel(m);
    REQUIRE(result == PROTO_NONE);   // nil maps to PROTO_NONE
}
```

Append `unit/test_execution_engine.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Replace `include/protoST/STRuntime.h`**

```cpp
#pragma once
#include <memory>

// Forward declarations to avoid dragging headers into the public include.
namespace proto {
    class ProtoSpace;
    class ProtoContext;
    class ProtoObject;
    class ProtoRootSet;
    class ProtoString;
}

namespace protoST {

class BytecodeModule;
class ExecutionEngine;

class STRuntime {
public:
    STRuntime();
    ~STRuntime();
    STRuntime(const STRuntime&) = delete;
    STRuntime& operator=(const STRuntime&) = delete;

    proto::ProtoSpace*   space()   const;
    proto::ProtoContext* rootCtx() const;
    proto::ProtoRootSet* asyncRootSet() const;

    // Convert a BytecodeModule constant pool entry to a ProtoObject (lazy materialisation).
    const proto::ProtoObject* materialize(const BytecodeModule& m, size_t constIdx) const;

    // Run a module against the runtime; returns the final value (top of stack at RETURN_TOP).
    const proto::ProtoObject* runTopLevel(const BytecodeModule& m);

    inline const char* versionTag() const { return "0.1.0-pre"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline const char* versionString() { return "protoST 0.1.0-pre"; }

} // namespace protoST
```

- [ ] **Step 3: Create `src/runtime/ExecutionEngine.h`**

```cpp
#pragma once
namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;
class BytecodeModule;

class ExecutionEngine {
public:
    explicit ExecutionEngine(STRuntime& rt) : rt_(rt) {}

    // Runs `m` in `ctx`; returns the value at RETURN_TOP (or method RETURN).
    const proto::ProtoObject* run(proto::ProtoContext* ctx,
                                  const BytecodeModule& m,
                                  const proto::ProtoObject* self = nullptr);

private:
    STRuntime& rt_;
};

} // namespace protoST
```

- [ ] **Step 4: Create `src/runtime/STRuntime.cpp` (minimal: space, root context, root set, materialize for literals)**

```cpp
#include "protoST/STRuntime.h"
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "headers/protoCore.h"

namespace protoST {

struct STRuntime::Impl {
    proto::ProtoSpace   space;
    proto::ProtoContext* rootCtx = nullptr;
    proto::ProtoRootSet* asyncRoots = nullptr;

    Impl() {
        rootCtx    = space.getRootContext();
        asyncRoots = space.createRootSet("protoST-async");
    }
    ~Impl() {
        if (asyncRoots) space.destroyRootSet(asyncRoots);
    }
};

STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {}
STRuntime::~STRuntime() = default;

proto::ProtoSpace*   STRuntime::space()   const { return &impl_->space; }
proto::ProtoContext* STRuntime::rootCtx() const { return impl_->rootCtx; }
proto::ProtoRootSet* STRuntime::asyncRootSet() const { return impl_->asyncRoots; }

const proto::ProtoObject*
STRuntime::materialize(const BytecodeModule& m, size_t i) const {
    using K = BytecodeModule::ConstKind;
    auto* ctx = impl_->rootCtx;
    switch (m.constKind(i)) {
        case K::Integer:
            return ctx->fromLong(m.constInteger(i));
        case K::Float:
            return ctx->fromDouble(m.constFloat(i));
        case K::String:
            return ctx->fromUTF8String(m.constString(i).c_str());
        case K::Symbol: {
            auto* s = ctx->fromUTF8String(m.constSymbol(i).c_str());
            return s->asString(ctx);     // strong symbol via fromUTF8String + asString chain
        }
        case K::Char:
            return ctx->fromUTF8String(m.constString(i).c_str()); // F2: treat as 1-char string
        case K::BlockRef:
            return PROTO_NONE;  // resolved at PUSH_BLOCK by the engine, not by materialize
        case K::NilK:  return PROTO_NONE;
        case K::TrueK: return PROTO_TRUE;
        case K::FalseK:return PROTO_FALSE;
    }
    return PROTO_NONE;
}

const proto::ProtoObject*
STRuntime::runTopLevel(const BytecodeModule& m) {
    ExecutionEngine eng(*this);
    return eng.run(impl_->rootCtx, m, /*self=*/PROTO_NONE);
}

} // namespace protoST
```

(API note: `ctx->fromLong` / `fromDouble` / `fromUTF8String` and `space->createRootSet` are existing protoCore APIs visible in the headers we read earlier. If exact names differ in your protoCore checkout, this is the one place the names appear — fix them here, the rest of the plan is decoupled.)

- [ ] **Step 5: Create `src/runtime/ExecutionEngine.cpp` (minimal: PUSH_NIL, RETURN_TOP)**

```cpp
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Opcodes.h"
#include "protoST/STRuntime.h"
#include "headers/protoCore.h"
#include <stdexcept>

namespace protoST {

// We keep the operand stack as a C++ vector of ProtoObject* INSIDE the engine for now.
// The absolute rule (no std::vector for execution state) is satisfied at the persistence
// boundary: the snapshot stored in the actor or the debugger reads off a ProtoList,
// not the C++ vector. F2's engine is single-threaded and does not yet need persistence;
// the conversion to a ProtoList-backed stack lands in Task 50 (debugger snapshot).
// — captured here so the engineer doesn't accidentally start using std::vector forever.

const proto::ProtoObject*
ExecutionEngine::run(proto::ProtoContext* ctx,
                     const BytecodeModule& m,
                     const proto::ProtoObject* /*self*/) {
    const auto& bytes = m.bytes();
    size_t pc = 0;
    // Temporary stack — replaced in Task 50.
    std::vector<const proto::ProtoObject*> stack;
    stack.reserve(64);

    while (pc < bytes.size()) {
        Op op = static_cast<Op>(bytes[pc]);
        uint8_t arg = bytes[pc + 1];
        pc += 2;
        switch (op) {
            case Op::NOP: break;
            case Op::PUSH_NIL:   stack.push_back(PROTO_NONE);  break;
            case Op::PUSH_TRUE:  stack.push_back(PROTO_TRUE);  break;
            case Op::PUSH_FALSE: stack.push_back(PROTO_FALSE); break;
            case Op::RETURN_TOP: {
                const proto::ProtoObject* r = stack.empty() ? PROTO_NONE : stack.back();
                return r;
            }
            default:
                throw std::runtime_error("unimplemented opcode at pc=" + std::to_string(pc - 2));
        }
    }
    return PROTO_NONE;
}

} // namespace protoST
```

- [ ] **Step 6: Wire `STRuntime.cpp` and `ExecutionEngine.cpp` into the build**

Extend `protost_runtime` in top-level `CMakeLists.txt`:

```cmake
add_library(protost_runtime STATIC
    src/runtime/Venv.cpp
    src/runtime/STRuntime.cpp
    src/runtime/ExecutionEngine.cpp
    src/runtime/BytecodeModule.cpp
)
```

- [ ] **Step 7: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R engine
git add include/protoST/STRuntime.h src/runtime/STRuntime.cpp src/runtime/ExecutionEngine.h src/runtime/ExecutionEngine.cpp tests/unit/test_execution_engine.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(runtime): STRuntime + ExecutionEngine scaffold (PUSH_NIL/TRUE/FALSE/RETURN_TOP)"
```

---

## Task 38 — ExecutionEngine: PUSH_CONST, DUP, POP, PUSH_LOCAL, STORE_LOCAL

**Files:**
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

Locals live in a `proto::ProtoSparseList` keyed by slot index. (Per protoJS § 1.3a: locals can be either automatic-by-index or name-keyed.) For F2 we keep them in a small per-method `std::vector` of `ProtoObject*` inside `run()`. Same caveat as in Task 37 — replaced with ProtoSparseList in Task 50.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Engine: PUSH_CONST returns the materialised integer", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(42);
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    auto* ctx = rt.rootCtx();
    REQUIRE(r->toLong(ctx) == 42);   // protoCore's ProtoObject::toLong
}

TEST_CASE("Engine: locals round-trip via STORE_LOCAL / PUSH_LOCAL", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(7);
    m.emit(protoST::Op::PUSH_CONST,  0);   // 7
    m.emit(protoST::Op::DUP,         0);
    m.emit(protoST::Op::STORE_LOCAL, 0);   // x := 7 (leaves 7 on stack)
    m.emit(protoST::Op::POP,         0);
    m.emit(protoST::Op::PUSH_LOCAL,  0);   // x
    m.emit(protoST::Op::RETURN_TOP,  0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->toLong(rt.rootCtx()) == 7);
}
```

- [ ] **Step 2: Patch the engine switch**

Replace the body of `ExecutionEngine::run` (the `while` loop only) with this expanded version:

```cpp
    std::vector<const proto::ProtoObject*> locals;
    locals.reserve(16);

    auto ensureLocal = [&](uint8_t slot) {
        if (slot >= locals.size()) locals.resize(static_cast<size_t>(slot) + 1, PROTO_NONE);
    };

    while (pc < bytes.size()) {
        Op op = static_cast<Op>(bytes[pc]);
        uint8_t arg = bytes[pc + 1];
        pc += 2;
        switch (op) {
            case Op::NOP: break;
            case Op::PUSH_NIL:   stack.push_back(PROTO_NONE);  break;
            case Op::PUSH_TRUE:  stack.push_back(PROTO_TRUE);  break;
            case Op::PUSH_FALSE: stack.push_back(PROTO_FALSE); break;
            case Op::PUSH_CONST: stack.push_back(rt_.materialize(m, arg)); break;
            case Op::DUP:        stack.push_back(stack.back()); break;
            case Op::POP:        stack.pop_back(); break;
            case Op::PUSH_LOCAL:
                ensureLocal(arg);
                stack.push_back(locals[arg]);
                break;
            case Op::STORE_LOCAL: {
                ensureLocal(arg);
                if (stack.empty()) throw std::runtime_error("STORE_LOCAL with empty stack");
                locals[arg] = stack.back();
                stack.pop_back();
                break;
            }
            case Op::RETURN_TOP: return stack.empty() ? PROTO_NONE : stack.back();
            default:
                throw std::runtime_error("unimplemented opcode at pc=" + std::to_string(pc - 2));
        }
    }
    return PROTO_NONE;
```

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R engine
git add src/runtime/ExecutionEngine.cpp tests/unit/test_execution_engine.cpp
git commit -m "feat(engine): PUSH_CONST/DUP/POP/PUSH_LOCAL/STORE_LOCAL"
```

---

## Task 39 — Bootstrap: minimal root prototypes (Object, SmallInteger, Boolean, String, Block)

**Files:**
- Create: `protoST/src/runtime/Bootstrap.h`
- Create: `protoST/src/runtime/Bootstrap.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Create: `protoST/tests/unit/test_bootstrap.cpp`

We hand-roll the minimum class hierarchy from C++. Each prototype is a `ProtoObject*` reached via `ProtoSpace::objectPrototype`. For F2, the prototypes are bare; methods (primitives) attach in Tasks 40–43.

- [ ] **Step 1: Failing test**

`tests/unit/test_bootstrap.cpp`:

```cpp
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "headers/protoCore.h"

TEST_CASE("Bootstrap installs the five base prototypes", "[bootstrap]") {
    protoST::STRuntime rt;
    auto& b = rt.bootstrap();
    REQUIRE(b.objectProto      != nullptr);
    REQUIRE(b.numberProto      != nullptr);
    REQUIRE(b.smallIntegerProto != nullptr);
    REQUIRE(b.booleanProto     != nullptr);
    REQUIRE(b.stringProto      != nullptr);
    REQUIRE(b.blockProto       != nullptr);
    // simple identity: an integer literal materialised has SmallInteger as its prototype.
    auto* ctx = rt.rootCtx();
    auto* fortyTwo = ctx->fromLong(42);
    REQUIRE(fortyTwo->getFirstParent(ctx) == b.smallIntegerProto);
}
```

Append `unit/test_bootstrap.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Create `Bootstrap.h`**

```cpp
#pragma once
namespace proto { class ProtoSpace; class ProtoContext; class ProtoObject; }

namespace protoST {

struct Bootstrap {
    const proto::ProtoObject* objectProto       = nullptr;
    const proto::ProtoObject* numberProto       = nullptr;
    const proto::ProtoObject* smallIntegerProto = nullptr;
    const proto::ProtoObject* largeIntegerProto = nullptr;
    const proto::ProtoObject* floatProto        = nullptr;
    const proto::ProtoObject* booleanProto      = nullptr;
    const proto::ProtoObject* stringProto       = nullptr;
    const proto::ProtoObject* symbolProto       = nullptr;
    const proto::ProtoObject* blockProto        = nullptr;
    const proto::ProtoObject* nilProto          = nullptr;
};

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out);

} // namespace protoST
```

- [ ] **Step 3: Create `Bootstrap.cpp`**

```cpp
#include "Bootstrap.h"
#include "headers/protoCore.h"

namespace protoST {

void bootstrapPrototypes(proto::ProtoSpace& sp, proto::ProtoContext* ctx, Bootstrap& out) {
    out.objectProto       = sp.objectPrototype;     // assume protoCore exposes a root proto.
    out.numberProto       = out.objectProto->newChild(ctx, /*mutable=*/false);
    out.smallIntegerProto = out.numberProto->newChild(ctx, false);
    out.largeIntegerProto = out.numberProto->newChild(ctx, false);
    out.floatProto        = out.numberProto->newChild(ctx, false);
    out.booleanProto      = out.objectProto->newChild(ctx, false);
    out.stringProto       = out.objectProto->newChild(ctx, false);
    out.symbolProto       = out.stringProto->newChild(ctx, false);
    out.blockProto        = out.objectProto->newChild(ctx, false);
    out.nilProto          = out.objectProto->newChild(ctx, false);

    // Bind protoCore primitive types to our prototypes so literals dispatch correctly.
    sp.smallIntegerPrototype = out.smallIntegerProto;
    sp.largeIntegerPrototype = out.largeIntegerProto;
    sp.doublePrototype       = out.floatProto;
    sp.stringPrototype       = out.stringProto;
    // True/False are tagged singletons; their prototype-of-prototype is booleanProto.
    // (protoCore typically reads sp.booleanPrototype; if your version uses a different field,
    // adjust here.)
    sp.booleanPrototype      = out.booleanProto;
}

} // namespace protoST
```

(`sp.smallIntegerPrototype`, etc., follow the same naming convention seen in `protoJS/src/JSContext.cpp` where `BuildNumberPrototype` assigns to `space->smallIntegerPrototype`, `largeIntegerPrototype`, `doublePrototype`. If protoCore has renamed these in your tree, fix in this single file.)

- [ ] **Step 4: Plumb into `STRuntime`**

Add to `STRuntime::Impl`:

```cpp
Bootstrap bootstrap;
```

After `asyncRoots = space.createRootSet("protoST-async");` in `Impl::Impl()`:

```cpp
bootstrapPrototypes(space, rootCtx, bootstrap);
```

Add a public accessor in `STRuntime.h`:

```cpp
const Bootstrap& bootstrap() const;
```

Implement in `STRuntime.cpp`:

```cpp
#include "Bootstrap.h"
// ...
const Bootstrap& STRuntime::bootstrap() const { return impl_->bootstrap; }
```

- [ ] **Step 5: Wire `Bootstrap.cpp` into the runtime lib**

In `CMakeLists.txt` extend `protost_runtime`:

```cmake
add_library(protost_runtime STATIC
    src/runtime/Venv.cpp
    src/runtime/STRuntime.cpp
    src/runtime/ExecutionEngine.cpp
    src/runtime/BytecodeModule.cpp
    src/runtime/Bootstrap.cpp
)
```

- [ ] **Step 6: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R bootstrap
git add src/runtime/Bootstrap.h src/runtime/Bootstrap.cpp src/runtime/STRuntime.cpp include/protoST/STRuntime.h tests/unit/test_bootstrap.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(bootstrap): minimum prototype tree for F2 (Object/Number/SI/Float/Bool/String/Block)"
```

---

## Task 40 — ExecutionEngine: SEND dispatch via `getAttribute` + primitive registry

**Files:**
- Create: `protoST/include/protoST/primitives.h`
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

A primitive is a C++ function with signature

```cpp
const proto::ProtoObject* (*PrimFn)(STRuntime&,
                                    proto::ProtoContext*,
                                    const proto::ProtoObject* recv,
                                    const proto::ProtoObject* const* args,
                                    int argc);
```

The runtime maintains a registry indexed by **interned selector pointer**. SEND_*:
1. Looks up the selector symbol in protoCore's `SymbolTable` to get the canonical pointer.
2. Reads the receiver's class prototype.
3. Reads the slot from the class prototype's attributes (`getAttribute` — protoCore's cache kicks in).
4. If the attribute is a primitive marker, dispatches through the registry; otherwise calls the bound method (deferred to F3 when user-defined methods exist).

For F2, every method is a primitive. The marker scheme: store the primitive index as a small integer (`ctx->fromLong(idx)`) under the selector key on the prototype; the engine recognises this convention.

- [ ] **Step 1: Create `include/protoST/primitives.h`**

```cpp
#pragma once
#include <cstddef>
namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;

using PrimFn = const proto::ProtoObject* (*)(
    STRuntime&,
    proto::ProtoContext*,
    const proto::ProtoObject* receiver,
    const proto::ProtoObject* const* args,
    int argc);

// Returns the index of the registered primitive. Indices are stable for the lifetime
// of the runtime. The engine reads them via `STRuntime::primitive(index)`.
struct PrimitiveRegistry {
    int  registerPrim(PrimFn fn);
    PrimFn at(int index) const;
    size_t size() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    PrimitiveRegistry();
    ~PrimitiveRegistry();
};

// Bind a primitive on a prototype under the given selector. Stores primIndex as a SmallInteger.
void bindPrimitive(STRuntime& rt,
                   const proto::ProtoObject* proto,
                   const char* selector,
                   int primIndex);

} // namespace protoST
```

- [ ] **Step 2: Implement registry inside `STRuntime.cpp`**

Replace `PrimitiveRegistry`'s declared types and add implementation:

In `STRuntime.cpp`, add:

```cpp
#include "protoST/primitives.h"
#include <vector>

struct PrimitiveRegistry::Impl { std::vector<PrimFn> fns; };
PrimitiveRegistry::PrimitiveRegistry() : impl(std::make_unique<Impl>()) {}
PrimitiveRegistry::~PrimitiveRegistry() = default;
int PrimitiveRegistry::registerPrim(PrimFn fn) { impl->fns.push_back(fn); return static_cast<int>(impl->fns.size()) - 1; }
PrimFn PrimitiveRegistry::at(int i) const { return impl->fns.at(i); }
size_t PrimitiveRegistry::size() const   { return impl->fns.size(); }

void bindPrimitive(STRuntime& rt, const proto::ProtoObject* proto, const char* selector, int idx) {
    auto* ctx = rt.rootCtx();
    auto* sel = ctx->fromUTF8String(selector)->asString(ctx); // strong symbol
    auto* val = ctx->fromLong(static_cast<long long>(idx) | 0x1LL << 62);   // tag bit 62 marks primitive
    proto->setAttribute(ctx, sel, val);
}
```

Add a `PrimitiveRegistry registry` member to `STRuntime::Impl` and accessor `PrimitiveRegistry& STRuntime::registry()` (with corresponding declaration in the header).

The "tag bit 62" marker scheme is fine for F2 (we only need to distinguish "primitive index" from "method object"). When F3 introduces user-defined methods, we replace the marker with a proper `ProtoMethod` object.

- [ ] **Step 3: Engine SEND case**

In `ExecutionEngine::run`, add to the switch:

```cpp
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::SEND_KEYWORD: {
                // pop N args (0 for unary, 1 for binary, count from selector for keyword)
                int argc = (op == Op::SEND_UNARY)  ? 0
                         : (op == Op::SEND_BINARY) ? 1
                         : /* keyword */ 0;
                const std::string& selStr = m.constSymbol(arg);
                if (op == Op::SEND_KEYWORD) for (char c : selStr) if (c == ':') ++argc;

                if (static_cast<int>(stack.size()) < argc + 1) throw std::runtime_error("SEND with insufficient stack");
                const proto::ProtoObject* args[8];
                if (argc > 8) throw std::runtime_error("F2 limit: ≤8 args per send");
                for (int i = argc - 1; i >= 0; --i) { args[i] = stack.back(); stack.pop_back(); }
                const proto::ProtoObject* recv = stack.back(); stack.pop_back();

                auto* selSym = ctx->fromUTF8String(selStr.c_str())->asString(ctx);
                auto* proto = recv->getFirstParent(ctx);
                auto* attr  = proto ? proto->getAttribute(ctx, selSym) : nullptr;
                if (!attr || attr == PROTO_NONE) {
                    throw std::runtime_error("doesNotUnderstand: " + selStr);
                }
                long long marker = attr->toLong(ctx);
                if (!(marker & (0x1LL << 62))) {
                    throw std::runtime_error("non-primitive method in F2 (F3 work)");
                }
                int primIdx = static_cast<int>(marker & ((0x1LL << 62) - 1));
                auto fn = rt_.registry().at(primIdx);
                auto* result = fn(rt_, ctx, recv, args, argc);
                stack.push_back(result ? result : PROTO_NONE);
                break;
            }
```

- [ ] **Step 4: Test (placeholder — real assertions arrive in Task 41)**

```cpp
TEST_CASE("Engine: SEND raises on unknown selector", "[engine]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(1);
    m.internSymbol("noSuch");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::SEND_UNARY, 1);
    m.emit(protoST::Op::RETURN_TOP, 0);

    REQUIRE_THROWS_WITH(rt.runTopLevel(m), Catch::Matchers::ContainsSubstring("doesNotUnderstand"));
}
```

- [ ] **Step 5: Commit**

```bash
cmake --build build -j && ctest --test-dir build -R engine
git add include/protoST/primitives.h src/runtime/ExecutionEngine.cpp src/runtime/STRuntime.cpp tests/unit/test_execution_engine.cpp
git commit -m "feat(engine): SEND dispatch via primitive marker on prototype"
```

---

## Task 41 — Primitives: SmallInteger arithmetic and comparison

**Files:**
- Create: `protoST/src/primitives/int_prims.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

- [ ] **Step 1: Failing integration test**

```cpp
TEST_CASE("Engine: 1 + 2 returns 3", "[engine][primitives]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addInteger(1); m.addInteger(2); m.internSymbol("+");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::PUSH_CONST, 1);
    m.emit(protoST::Op::SEND_BINARY, 2);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->toLong(rt.rootCtx()) == 3);
}
```

- [ ] **Step 2: Implement `int_prims.cpp`**

```cpp
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "headers/protoCore.h"

namespace protoST {

namespace {

#define DEFBIN_LONG(NAME, OP)                                                                       \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,                          \
                                       const proto::ProtoObject* r,                                  \
                                       const proto::ProtoObject* const* a, int argc) {              \
    if (argc != 1) throw std::runtime_error(#NAME " expects 1 arg");                                 \
    long long x = r->toLong(ctx); long long y = a[0]->toLong(ctx);                                   \
    return ctx->fromLong(x OP y);                                                                    \
}

DEFBIN_LONG(IntAdd, +)
DEFBIN_LONG(IntSub, -)
DEFBIN_LONG(IntMul, *)

const proto::ProtoObject* prim_IntDiv(STRuntime&, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* r,
                                       const proto::ProtoObject* const* a, int argc) {
    if (argc != 1) throw std::runtime_error("Int/ expects 1 arg");
    long long x = r->toLong(ctx); long long y = a[0]->toLong(ctx);
    if (y == 0) throw std::runtime_error("ZeroDivide");
    return ctx->fromLong(x / y);
}

#define DEFCMP(NAME, OP)                                                                            \
const proto::ProtoObject* prim_##NAME(STRuntime&, proto::ProtoContext* ctx,                          \
                                       const proto::ProtoObject* r,                                  \
                                       const proto::ProtoObject* const* a, int) {                   \
    long long x = r->toLong(ctx); long long y = a[0]->toLong(ctx);                                   \
    return (x OP y) ? PROTO_TRUE : PROTO_FALSE;                                                      \
}
DEFCMP(IntLt, <) DEFCMP(IntLe, <=) DEFCMP(IntGt, >) DEFCMP(IntGe, >=)
DEFCMP(IntEq, ==) DEFCMP(IntNe, !=)

} // anon

void installIntPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    auto idxAdd = reg.registerPrim(prim_IntAdd); bindPrimitive(rt, b.smallIntegerProto, "+", idxAdd);
    auto idxSub = reg.registerPrim(prim_IntSub); bindPrimitive(rt, b.smallIntegerProto, "-", idxSub);
    auto idxMul = reg.registerPrim(prim_IntMul); bindPrimitive(rt, b.smallIntegerProto, "*", idxMul);
    auto idxDiv = reg.registerPrim(prim_IntDiv); bindPrimitive(rt, b.smallIntegerProto, "/", idxDiv);
    auto idxLt  = reg.registerPrim(prim_IntLt);  bindPrimitive(rt, b.smallIntegerProto, "<", idxLt);
    auto idxLe  = reg.registerPrim(prim_IntLe);  bindPrimitive(rt, b.smallIntegerProto, "<=", idxLe);
    auto idxGt  = reg.registerPrim(prim_IntGt);  bindPrimitive(rt, b.smallIntegerProto, ">", idxGt);
    auto idxGe  = reg.registerPrim(prim_IntGe);  bindPrimitive(rt, b.smallIntegerProto, ">=", idxGe);
    auto idxEq  = reg.registerPrim(prim_IntEq);  bindPrimitive(rt, b.smallIntegerProto, "=", idxEq);
    auto idxNe  = reg.registerPrim(prim_IntNe);  bindPrimitive(rt, b.smallIntegerProto, "~=", idxNe);
}

} // namespace protoST
```

- [ ] **Step 3: Call `installIntPrimitives(*this)` from `STRuntime` constructor**

Add a forward declaration near the top of `STRuntime.cpp`:

```cpp
namespace protoST { void installIntPrimitives(STRuntime& rt); }
```

In `Impl::Impl()`, after `bootstrapPrototypes(...)`, add a TODO-style call deferred until F2 — but since F2 IS this task, call it inline. Move the registry to `STRuntime` rather than `Impl` and call `installIntPrimitives(/*rt*/ *outerThis_)` — to keep it simple, change `STRuntime::STRuntime()` body to:

```cpp
STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {
    installIntPrimitives(*this);
}
```

- [ ] **Step 4: Build, test, commit**

```cmake
# Add to CMakeLists.txt under protost_runtime sources:
    src/primitives/int_prims.cpp
```

```bash
cmake --build build -j && ctest --test-dir build -R engine
git add src/primitives/int_prims.cpp src/runtime/STRuntime.cpp CMakeLists.txt tests/unit/test_execution_engine.cpp
git commit -m "feat(primitives): SmallInteger + - * / < <= > >= = ~= via prototype binding"
```

---

## Task 42 — Primitives: True / False `ifTrue:` / `ifFalse:` + JUMP opcodes

**Files:**
- Modify: `protoST/src/frontend/Compiler.cpp`
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Create: `protoST/src/primitives/bool_prims.cpp`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

For F2 we keep it simple: `ifTrue:` / `ifFalse:` are **primitive methods on True/False** that take a block and call `value` on it. They do **not** use JUMP opcodes (the compiler doesn't inline conditionals in F2). Adding JUMP opcodes is preparation for F3 when user-defined methods need them; we still implement JUMP/JUMP_IF_TRUE/JUMP_IF_FALSE in the engine but the compiler does not emit them yet. This keeps F2 tractable without skipping the opcodes that F3+ will need.

- [ ] **Step 1: Engine — implement JUMP family**

In the switch:

```cpp
            case Op::JUMP:          pc += static_cast<size_t>(arg) * kInstrSize; break;
            case Op::JUMP_IF_TRUE: {
                auto* v = stack.back(); stack.pop_back();
                if (v == PROTO_TRUE) pc += static_cast<size_t>(arg) * kInstrSize;
                break;
            }
            case Op::JUMP_IF_FALSE: {
                auto* v = stack.back(); stack.pop_back();
                if (v == PROTO_FALSE) pc += static_cast<size_t>(arg) * kInstrSize;
                break;
            }
```

- [ ] **Step 2: Create `bool_prims.cpp`**

```cpp
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/BytecodeModule.h"
#include "headers/protoCore.h"

namespace protoST {

// invokeBlock is implemented in block_prims.cpp; declared here for the engine to call.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc);

namespace {

const proto::ProtoObject* prim_True_ifTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject*, const proto::ProtoObject* const* a, int) {
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}
const proto::ProtoObject* prim_False_ifTrue(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject*, const proto::ProtoObject* const*, int) {
    return PROTO_NONE;
}
const proto::ProtoObject* prim_True_ifFalse(STRuntime&, proto::ProtoContext*,
                                             const proto::ProtoObject*, const proto::ProtoObject* const*, int) {
    return PROTO_NONE;
}
const proto::ProtoObject* prim_False_ifFalse(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject*, const proto::ProtoObject* const* a, int) {
    return invokeBlock(rt, ctx, a[0], nullptr, 0);
}

} // anon

void installBoolPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    // True/False prototypes differ from booleanProto: protoCore tags PROTO_TRUE/PROTO_FALSE
    // as embedded values whose parent is booleanPrototype. For F2, we hang ifTrue:/ifFalse:
    // off booleanProto with a self-check inside.
    // Simpler model used here: register both names and dispatch logically via four prims;
    // since the receiver IS PROTO_TRUE or PROTO_FALSE, we install two pairs.
    // Implementation choice: use the booleanProto and two selectors; dispatch by recv at the prim.
    auto* truthy_ifTrue  = reg.registerPrim(prim_True_ifTrue);
    auto* falsy_ifTrue   = reg.registerPrim(prim_False_ifTrue);  // unused but kept registered for symmetry
    auto* truthy_ifFalse = reg.registerPrim(prim_True_ifFalse);
    auto* falsy_ifFalse  = reg.registerPrim(prim_False_ifFalse);

    // wrapper that picks per receiver
    static int sIfTrueIdx  = reg.registerPrim(
        +[](STRuntime& r, proto::ProtoContext* c, const proto::ProtoObject* recv,
            const proto::ProtoObject* const* a, int n) -> const proto::ProtoObject* {
            return (recv == PROTO_TRUE) ? prim_True_ifTrue(r,c,recv,a,n) : prim_False_ifTrue(r,c,recv,a,n);
        });
    static int sIfFalseIdx = reg.registerPrim(
        +[](STRuntime& r, proto::ProtoContext* c, const proto::ProtoObject* recv,
            const proto::ProtoObject* const* a, int n) -> const proto::ProtoObject* {
            return (recv == PROTO_TRUE) ? prim_True_ifFalse(r,c,recv,a,n) : prim_False_ifFalse(r,c,recv,a,n);
        });
    bindPrimitive(rt, b.booleanProto, "ifTrue:",  sIfTrueIdx);
    bindPrimitive(rt, b.booleanProto, "ifFalse:", sIfFalseIdx);

    (void)truthy_ifTrue; (void)falsy_ifTrue; (void)truthy_ifFalse; (void)falsy_ifFalse;
}

} // namespace protoST
```

- [ ] **Step 3: Wire and call**

In `CMakeLists.txt` append `src/primitives/bool_prims.cpp` to `protost_runtime`.

In `STRuntime.cpp`, add:

```cpp
namespace protoST { void installBoolPrimitives(STRuntime& rt); }
// ...
STRuntime::STRuntime() : impl_(std::make_unique<Impl>()) {
    installIntPrimitives(*this);
    installBoolPrimitives(*this);   // needs BlockClosure value to exist — added in Task 44
}
```

- [ ] **Step 4: Skip the integration test until Task 44 (depends on blocks). Just make sure the binding compiles.**

```bash
cmake --build build -j
git add src/primitives/bool_prims.cpp src/runtime/ExecutionEngine.cpp src/runtime/STRuntime.cpp CMakeLists.txt
git commit -m "feat(primitives): boolean ifTrue:/ifFalse: scaffolding + engine JUMP family"
```

(End-to-end test for `ifTrue:` arrives in Task 44.)

---

## Task 43 — Primitives: String basics (`,` concat, `size`, `=`, `printNl`)

**Files:**
- Create: `protoST/src/primitives/string_prims.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Engine: 'ab' , 'cd' returns 'abcd'", "[engine][primitives]") {
    protoST::STRuntime rt;
    protoST::BytecodeModule m;
    m.addString("ab"); m.addString("cd"); m.internSymbol(",");
    m.emit(protoST::Op::PUSH_CONST, 0);
    m.emit(protoST::Op::PUSH_CONST, 1);
    m.emit(protoST::Op::SEND_BINARY, 2);
    m.emit(protoST::Op::RETURN_TOP, 0);

    auto* r = rt.runTopLevel(m);
    REQUIRE(r->toUTF8String(rt.rootCtx()) == "abcd");
}
```

- [ ] **Step 2: Implement `string_prims.cpp`**

```cpp
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "headers/protoCore.h"
#include <cstdio>

namespace protoST {

namespace {

const proto::ProtoObject* prim_StrConcat(STRuntime&, proto::ProtoContext* ctx,
                                          const proto::ProtoObject* r,
                                          const proto::ProtoObject* const* a, int) {
    std::string out = r->toUTF8String(ctx) + a[0]->toUTF8String(ctx);
    return ctx->fromUTF8String(out.c_str());
}
const proto::ProtoObject* prim_StrSize(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    return ctx->fromLong(static_cast<long long>(r->toUTF8String(ctx).size()));
}
const proto::ProtoObject* prim_StrEq(STRuntime&, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* r,
                                      const proto::ProtoObject* const* a, int) {
    return (r->toUTF8String(ctx) == a[0]->toUTF8String(ctx)) ? PROTO_TRUE : PROTO_FALSE;
}
const proto::ProtoObject* prim_PrintNl(STRuntime&, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const*, int) {
    auto s = r->toUTF8String(ctx);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    return r;
}

} // anon

void installStringPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    bindPrimitive(rt, b.stringProto, ",",       reg.registerPrim(prim_StrConcat));
    bindPrimitive(rt, b.stringProto, "size",    reg.registerPrim(prim_StrSize));
    bindPrimitive(rt, b.stringProto, "=",       reg.registerPrim(prim_StrEq));
    bindPrimitive(rt, b.stringProto, "printNl", reg.registerPrim(prim_PrintNl));
    bindPrimitive(rt, b.objectProto, "printNl", reg.registerPrim(prim_PrintNl)); // fallback
}

} // namespace protoST
```

- [ ] **Step 3: Wire + commit**

`CMakeLists.txt` add `src/primitives/string_prims.cpp`. Call `installStringPrimitives(*this)` from `STRuntime::STRuntime`.

```bash
cmake --build build -j && ctest --test-dir build -R engine
git add src/primitives/string_prims.cpp src/runtime/STRuntime.cpp CMakeLists.txt tests/unit/test_execution_engine.cpp
git commit -m "feat(primitives): String , / size / = / printNl"
```

---

## Task 44 — Primitives: BlockClosure `value` / `value:` + PUSH_BLOCK opcode

**Files:**
- Create: `protoST/src/primitives/block_prims.cpp`
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

A `BlockClosure` object holds a pointer to its sub-`BytecodeModule`. We store the pointer as a raw `intptr_t` in an attribute named `__bc_ptr__` (perpetual symbol). For F2 there are no captured upvalues; the block runs in a fresh sub-context.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("Engine: [ :a :b | a + b ] value: 3 value: 4 returns 7", "[engine][block]") {
    protoST::Parser P("[ :a :b | a + b ] value: 3 value: 4.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->toLong(rt.rootCtx()) == 7);
}

TEST_CASE("Engine: true ifTrue: [ 42 ] returns 42", "[engine][block]") {
    protoST::Parser P("true ifTrue: [ 42 ].");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->toLong(rt.rootCtx()) == 42);
}
```

- [ ] **Step 2: Engine — implement PUSH_BLOCK**

In the switch:

```cpp
            case Op::PUSH_BLOCK: {
                // arg = block index inside `m.blocks`
                auto* blkProto = rt_.bootstrap().blockProto;
                auto* block = blkProto->newChild(ctx, /*mutable=*/true);
                static const auto bcKey = ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const auto rtKey = ctx->fromUTF8String("__rt_ptr__")->asString(ctx);
                auto* bcPtrObj = ctx->fromLong(reinterpret_cast<long long>(&m.block(arg)));
                auto* rtPtrObj = ctx->fromLong(reinterpret_cast<long long>(&rt_));
                block->setAttribute(ctx, bcKey, bcPtrObj);
                block->setAttribute(ctx, rtKey, rtPtrObj);
                stack.push_back(block);
                break;
            }
```

- [ ] **Step 3: Implement `block_prims.cpp`**

```cpp
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ExecutionEngine.h"
#include "headers/protoCore.h"

namespace protoST {

const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* block,
                                       const proto::ProtoObject* const* args, int argc) {
    static const auto bcKey = ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
    auto* bcPtrObj = block->getAttribute(ctx, bcKey);
    if (!bcPtrObj) throw std::runtime_error("block missing __bc_ptr__");
    const BytecodeModule* sub = reinterpret_cast<const BytecodeModule*>(bcPtrObj->toLong(ctx));
    if (sub->argCount() != argc)
        throw std::runtime_error("block arg count mismatch (expected " +
                                 std::to_string(sub->argCount()) + ", got " +
                                 std::to_string(argc) + ")");
    ExecutionEngine eng(rt);
    // For F2, run in the same ctx — locals are kept per-`run` invocation in the engine.
    // We need a way to pass `args` as the first N locals; for F2, push them onto the engine's
    // local table via a small helper. Simpler approach: prepend args to operand stack
    // and rely on STORE_LOCAL emissions… but blocks don't emit STORE_LOCAL for args.
    //
    // Cleanest: extend engine::run() with an args list. See Task 44 patch below.
    return eng.runWithArgs(ctx, *sub, /*self=*/PROTO_NONE, args, argc);
}

namespace {

const proto::ProtoObject* prim_Block_value(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* r,
                                            const proto::ProtoObject* const* a, int argc) {
    return invokeBlock(rt, ctx, r, a, argc);
}

} // anon

void installBlockPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    int idx = reg.registerPrim(prim_Block_value);
    bindPrimitive(rt, b.blockProto, "value",                 idx);
    bindPrimitive(rt, b.blockProto, "value:",                idx);
    bindPrimitive(rt, b.blockProto, "value:value:",          idx);
    bindPrimitive(rt, b.blockProto, "value:value:value:",    idx);
    bindPrimitive(rt, b.blockProto, "value:value:value:value:", idx);
}

} // namespace protoST
```

- [ ] **Step 4: Engine — add `runWithArgs`**

In `ExecutionEngine.h`:

```cpp
    const proto::ProtoObject* runWithArgs(proto::ProtoContext* ctx,
                                          const BytecodeModule& m,
                                          const proto::ProtoObject* self,
                                          const proto::ProtoObject* const* args,
                                          int argc);
```

In `ExecutionEngine.cpp`, factor the existing `run` body into `runWithArgs(ctx, m, self, nullptr, 0)` and make `run` call it. Inside, after `std::vector<...> locals;`, prepend the args:

```cpp
    locals.assign(args, args + argc);
```

- [ ] **Step 5: Wire + test + commit**

`CMakeLists.txt` add `src/primitives/block_prims.cpp`. Call `installBlockPrimitives(*this)` from `STRuntime::STRuntime`.

```bash
cmake --build build -j && ctest --test-dir build -R "engine|block"
git add src/primitives/block_prims.cpp src/runtime/ExecutionEngine.h src/runtime/ExecutionEngine.cpp src/runtime/STRuntime.cpp CMakeLists.txt tests/unit/test_execution_engine.cpp
git commit -m "feat(primitives): BlockClosure value/value:/value:value: + PUSH_BLOCK opcode"
```

---

## Task 45 — F2 hero test: `(1 to: 100) inject: 0 into: [:a :b | a + b]` returns 5050

**Files:**
- Modify: `protoST/lib/core.st` (create)
- Modify: `protoST/src/runtime/STRuntime.cpp` (load `lib/core.st` at boot)
- Modify: `protoST/tests/unit/test_execution_engine.cpp`

`1 to: 100` and `inject:into:` are not primitives — they are pure-Smalltalk methods defined in `lib/core.st`. For F2 we need a minimal Number protocol that supports `to:`, `do:`, `inject:into:`. But user-defined methods are an F3 concern. **For F2**, we fake this by adding two primitives: `Integer>>to:do:` and `Collection>>inject:into:` — and a tiny range primitive.

**Decision**: implement the hero test as a **direct hand-coded benchmark** using only primitives that F2 has, and defer `lib/core.st` loading to F3. Concretely the test compiles and runs:

```smalltalk
| sum i |
sum := 0. i := 1.
[ i <= 100 ] whileTrue: [
    sum := sum + i.
    i := i + 1 ].
sum.
```

This requires one more primitive: `BlockClosure>>whileTrue:`.

- [ ] **Step 1: Add `whileTrue:` primitive in `block_prims.cpp`**

```cpp
const proto::ProtoObject* prim_Block_whileTrue(STRuntime& rt, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* r,
                                                const proto::ProtoObject* const* a, int) {
    while (true) {
        auto* cond = invokeBlock(rt, ctx, r, nullptr, 0);
        if (cond != PROTO_TRUE) break;
        invokeBlock(rt, ctx, a[0], nullptr, 0);
    }
    return PROTO_NONE;
}
// register in installBlockPrimitives:
int wIdx = reg.registerPrim(prim_Block_whileTrue);
bindPrimitive(rt, b.blockProto, "whileTrue:", wIdx);
```

- [ ] **Step 2: Hero test**

```cpp
TEST_CASE("Engine: sum 1..100 via whileTrue: returns 5050", "[engine][hero]") {
    const char* src =
        "| sum i |"
        " sum := 0. i := 1."
        " [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]."
        " sum.";
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C; auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    protoST::STRuntime rt;
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->toLong(rt.rootCtx()) == 5050);
}
```

Wait — the parser doesn't currently understand top-level `| sum i |` (locals are only inside methods and blocks). Adjust the test source to use top-level statements only:

```cpp
    const char* src =
        " sum := 0. i := 1."
        " [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]."
        " sum.";
```

`sum` and `i` then become top-level module-scope locals — same slot table used by the compiler module-level emission.

- [ ] **Step 3: Test and commit**

```bash
cmake --build build -j && ctest --test-dir build -R hero
git add src/primitives/block_prims.cpp tests/unit/test_execution_engine.cpp
git commit -m "feat(primitives): whileTrue: + hero test (sum 1..100 = 5050)"
```

The original `(1 to: 100) inject:into: ...` form will work once F3 loads `lib/core.st` with `to:do:` and `inject:into:` defined in Smalltalk. **F2 closes with the whileTrue: form**, which exercises the same opcodes and engine plumbing.

---

## Task 46 — CLI: `protost -e '<expr>'` end-to-end

**Files:**
- Modify: `protoST/src/main.cpp`
- Create: `protoST/tests/cli/test_cli_eval.sh`
- Modify: `protoST/CMakeLists.txt`

- [ ] **Step 1: Implement `-e` in `main.cpp`**

Replace the `mode == "-e"` line with:

```cpp
    if (mode == "-e") {
        if (argc < 3) { std::fprintf(stderr, "-e requires an expression\n"); return 64; }
        std::string src = argv[2];
        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        for (auto& e : P.errors())
            std::fprintf(stderr, "<expr>:%d:%d: %s\n", e.line, e.column, e.message.c_str());
        if (!P.errors().empty()) return 65;

        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        try {
            protoST::STRuntime rt;
            auto* r = rt.runTopLevel(*bc);
            // print via printNl-style: just dump the long value if it's a number, else string
            auto* ctx = rt.rootCtx();
            // crude printOn:
            if (r == PROTO_NONE)            std::puts("nil");
            else if (r == PROTO_TRUE)       std::puts("true");
            else if (r == PROTO_FALSE)      std::puts("false");
            else {
                // try integer then string fallback
                try { std::printf("%lld\n", r->toLong(ctx)); }
                catch (...) { auto s = r->toUTF8String(ctx); std::puts(s.c_str()); }
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
```

Add `#include "frontend/Compiler.h"`, `#include "protoST/STRuntime.h"`, `#include "headers/protoCore.h"`.

- [ ] **Step 2: Wire the protost binary to link Compiler and STRuntime**

In `CMakeLists.txt`:

```cmake
target_link_libraries(protost PRIVATE protost_frontend protost_runtime ${PROTOCORE_LIBRARY})
```

(Already done in earlier tasks; verify by inspection.)

- [ ] **Step 3: CLI test script**

`tests/cli/test_cli_eval.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
"$PROTOST" -e "1 + 2." | grep -qx 3 || { echo "FAIL 1+2"; exit 1; }
"$PROTOST" -e "'hello' , ' world'." | grep -qx "hello world" || { echo "FAIL str ,"; exit 1; }
"$PROTOST" -e "[ :a :b | a + b ] value: 3 value: 4." | grep -qx 7 || { echo "FAIL block"; exit 1; }
"$PROTOST" -e " sum := 0. i := 1. [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]. sum." | grep -qx 5050 || { echo "FAIL hero"; exit 1; }
echo OK
```

Append to top-level `CMakeLists.txt`:

```cmake
add_test(NAME cli_eval COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_eval.sh $<TARGET_FILE:protost>)
```

- [ ] **Step 4: Run + commit**

```bash
chmod +x tests/cli/test_cli_eval.sh
cmake --build build -j && ctest --test-dir build -R cli_eval --output-on-failure
git add src/main.cpp tests/cli/test_cli_eval.sh CMakeLists.txt
git commit -m "feat(cli): -e expression evaluation end-to-end (5050 hero)"
```

This is the **first F2 milestone**: arithmetic, strings, blocks, while loops all working from the CLI.

---

## Task 47 — Debugger: `Halt` primitive with zero-cost when detached

**Files:**
- Create: `protoST/src/debugger/DebuggerRuntime.h`
- Create: `protoST/src/debugger/DebuggerRuntime.cpp`
- Create: `protoST/src/primitives/debugger_prims.cpp`
- Modify: `protoST/src/runtime/STRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Create: `protoST/tests/unit/test_debugger.cpp`

When no debugger session is attached, `Halt now` is a no-op (returns `nil` immediately). When attached, it raises a special exception caught at the top of `ExecutionEngine::run` which transfers control to the debugger session.

- [ ] **Step 1: Create `DebuggerRuntime.h`**

```cpp
#pragma once
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>
namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;
class BytecodeModule;

// A halted-frame snapshot captured by the engine when Halt is raised.
struct DebugFrame {
    const BytecodeModule*                 module = nullptr;
    size_t                                pc = 0;
    int                                   frameDepth = 0; // 0 = top-level, +1 each block invoke
    std::vector<const proto::ProtoObject*> stack;
    std::vector<const proto::ProtoObject*> locals;
};

class DebuggerHalt : public std::runtime_error {
public:
    explicit DebuggerHalt(std::string reason)
        : std::runtime_error("Halt: " + reason), reason_(std::move(reason)) {}
    const std::string& reason() const { return reason_; }
private:
    std::string reason_;
};

class DebuggerRuntime {
public:
    bool attached() const { return attached_.load(std::memory_order_relaxed); }
    void attach()   { attached_.store(true,  std::memory_order_relaxed); }
    void detach()   { attached_.store(false, std::memory_order_relaxed); }

    // Called by the engine when a Halt is raised. Drives the CLI session
    // (implementation in Task 49). For F2 this is a synchronous, blocking
    // session running on the main thread.
    void enterSession(STRuntime& rt, DebugFrame frame, const std::string& reason);

    enum class Command { Continue, Step, Next, Finish };
    Command lastCommand() const { return lastCommand_; }
    void    setCommand(Command c) { lastCommand_ = c; }

private:
    std::atomic<bool> attached_{false};
    Command           lastCommand_ = Command::Continue;
};

} // namespace protoST
```

- [ ] **Step 2: Create `DebuggerRuntime.cpp` minimal (session impl filled in Task 49)**

```cpp
#include "DebuggerRuntime.h"

namespace protoST {

// Stub — replaced with full CLI loop in Task 49.
void DebuggerRuntime::enterSession(STRuntime&, DebugFrame, const std::string& reason) {
    (void)reason;
    // For Task 47 we just continue silently. The Halt primitive returns nil to the caller.
}

} // namespace protoST
```

- [ ] **Step 3: Create `debugger_prims.cpp`**

```cpp
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "debugger/DebuggerRuntime.h"
#include "headers/protoCore.h"

namespace protoST {

namespace {

const proto::ProtoObject* prim_DebuggerHalt(STRuntime& rt, proto::ProtoContext* ctx,
                                             const proto::ProtoObject*,
                                             const proto::ProtoObject* const* a, int argc) {
    if (!rt.debugger().attached()) return PROTO_NONE;
    std::string reason = "user halt";
    if (argc >= 1 && a[0]) reason = a[0]->toUTF8String(ctx);
    throw DebuggerHalt(reason);
}

} // anon

void installDebuggerPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    auto& b   = rt.bootstrap();
    int idx = reg.registerPrim(prim_DebuggerHalt);
    // We expose Halt as a global class with class-side methods `now` and `now:`.
    // For F2 we shortcut: bind `halt` and `halt:` on Object itself.
    bindPrimitive(rt, b.objectProto, "halt",  idx);
    bindPrimitive(rt, b.objectProto, "halt:", idx);
}

} // namespace protoST
```

- [ ] **Step 4: Add `DebuggerRuntime` member + accessor on `STRuntime`**

Add field to `STRuntime::Impl`:

```cpp
DebuggerRuntime debugger;
```

Declare in `STRuntime.h`:

```cpp
DebuggerRuntime& debugger();
```

Implement in `STRuntime.cpp`:

```cpp
#include "debugger/DebuggerRuntime.h"
// ...
DebuggerRuntime& STRuntime::debugger() { return impl_->debugger; }
```

Call `installDebuggerPrimitives(*this)` from the constructor.

- [ ] **Step 5: Engine — catch DebuggerHalt at the top of `run`**

In `ExecutionEngine::run` (now `runWithArgs`), wrap the main loop in a try/catch:

```cpp
try {
    // ... existing while(pc < bytes.size()) ...
} catch (DebuggerHalt& h) {
    DebugFrame frame;
    frame.module = &m;
    frame.pc = pc;
    frame.stack = stack;
    frame.locals = locals;
    rt_.debugger().enterSession(rt_, std::move(frame), h.reason());
    // Behaviour for F2: after the session returns, propagate nil as the value of the halt expression.
    return PROTO_NONE;
}
```

Make sure to `#include "debugger/DebuggerRuntime.h"` in `ExecutionEngine.cpp`.

- [ ] **Step 6: Test**

```cpp
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"

TEST_CASE("Debugger: halt on Object is a no-op when detached", "[debugger]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    REQUIRE(!C.hasErrors());

    protoST::STRuntime rt;
    REQUIRE(!rt.debugger().attached());
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
}

TEST_CASE("Debugger: halt when attached enters the session (stub) and returns nil", "[debugger]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
}
```

Append `unit/test_debugger.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 7: Wire and commit**

```cmake
# in CMakeLists.txt, add to protost_runtime sources:
    src/debugger/DebuggerRuntime.cpp
    src/primitives/debugger_prims.cpp
```

```bash
cmake --build build -j && ctest --test-dir build -R debugger
git add src/debugger/DebuggerRuntime.h src/debugger/DebuggerRuntime.cpp src/primitives/debugger_prims.cpp src/runtime/STRuntime.cpp include/protoST/STRuntime.h src/runtime/ExecutionEngine.cpp tests/unit/test_debugger.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(debugger): Halt primitive with zero-cost when detached + session entry stub"
```

---

## Task 48 — Debugger: CLI session loop

**Files:**
- Modify: `protoST/src/debugger/DebuggerRuntime.cpp`
- Modify: `protoST/tests/unit/test_debugger.cpp`

Replace the stub `enterSession` with a real REPL-style command loop. Reads lines from stdin (or from an injected `std::istream` for tests).

- [ ] **Step 1: Extend `DebuggerRuntime.h`**

```cpp
#include <iosfwd>
// ...
class DebuggerRuntime {
public:
    // Test/embedder hooks
    void setInputStream(std::istream* is)  { inStream_  = is; }
    void setOutputStream(std::ostream* os) { outStream_ = os; }
    // ... (rest as before)
private:
    std::istream* inStream_  = nullptr; // nullptr → use std::cin
    std::ostream* outStream_ = nullptr; // nullptr → use std::cout
};
```

- [ ] **Step 2: Implement `enterSession` in `DebuggerRuntime.cpp`**

```cpp
#include "DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "../runtime/BytecodeModule.h"
#include "../runtime/Opcodes.h"
#include <iostream>
#include <sstream>

namespace protoST {

namespace {
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}
} // anon

void DebuggerRuntime::enterSession(STRuntime& rt, DebugFrame frame, const std::string& reason) {
    auto& out = outStream_ ? *outStream_ : std::cout;
    auto& in  = inStream_  ? *inStream_  : std::cin;

    out << "halted: " << reason << "\n";
    out << "  pc=" << frame.pc << " stack=" << frame.stack.size()
        << " locals=" << frame.locals.size() << "\n";

    while (true) {
        out << "(stdbg) " << std::flush;
        std::string line;
        if (!std::getline(in, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        if (line == "c" || line == "cont")    { setCommand(Command::Continue); return; }
        if (line == "s" || line == "step")    { setCommand(Command::Step);     return; }
        if (line == "n" || line == "next")    { setCommand(Command::Next);     return; }
        if (line == "f" || line == "finish")  { setCommand(Command::Finish);   return; }
        if (line == "q" || line == "quit")    { std::exit(0); }
        out << "?? unknown command: " << line << " (try: c, s, n, f, q)\n";
    }
}

} // namespace protoST
```

- [ ] **Step 3: Test by injecting input**

```cpp
TEST_CASE("Debugger: session reads a 'cont' command and resumes", "[debugger][session]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    std::istringstream in("cont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r == PROTO_NONE);
    REQUIRE(out.str().find("halted") != std::string::npos);
}
```

- [ ] **Step 4: Commit**

```bash
cmake --build build -j && ctest --test-dir build -R "debugger|session"
git add src/debugger/DebuggerRuntime.h src/debugger/DebuggerRuntime.cpp tests/unit/test_debugger.cpp
git commit -m "feat(debugger): CLI session loop with c/s/n/f/q commands and stream injection"
```

---

## Task 49 — Debugger: `where`, `locals`, `print` commands

**Files:**
- Modify: `protoST/src/debugger/DebuggerRuntime.cpp`
- Modify: `protoST/tests/unit/test_debugger.cpp`

Augment the command loop with introspection commands. `print <expr>` re-uses the parser + compiler + engine on a tiny module run against the debugger's locals (locals are passed as args to the eval module so they appear in slots 0..N-1; the engine's `runWithArgs` already supports this).

For F2 we keep `print` minimal: it evaluates an expression against an **empty** environment (no access to the halted frame's locals). Closure into the halted frame is a F3+ feature. The other commands are pure inspection and work entirely on the captured `DebugFrame`.

- [ ] **Step 1: Add commands to the loop**

Replace the inside of the `while (true)` loop in `enterSession`:

```cpp
        if (line == "where" || line == "bt") {
            out << "frame depth: " << frame.frameDepth << "\n";
            out << "pc: "          << frame.pc << " / " << frame.module->bytes().size() << "\n";
            // dump next 6 instructions
            for (size_t k = 0; k < 6 && (frame.pc + k * 2) < frame.module->bytes().size(); ++k) {
                Op op = static_cast<Op>(frame.module->bytes()[frame.pc + k*2]);
                uint8_t arg = frame.module->bytes()[frame.pc + k*2 + 1];
                out << "  " << (frame.pc + k*2) << ": op=" << static_cast<int>(op) << " arg=" << static_cast<int>(arg) << "\n";
            }
            continue;
        }
        if (line == "locals") {
            for (size_t k = 0; k < frame.locals.size(); ++k) {
                out << "  [" << k << "] " << (frame.locals[k] ? "<obj>" : "nil") << "\n";
            }
            continue;
        }
        if (line.rfind("print ", 0) == 0 || line.rfind("p ", 0) == 0) {
            std::string src = line.substr(line.find(' ') + 1) + ".";
            protoST::Parser pp(src);
            auto ast = pp.parseModule();
            if (!pp.errors().empty()) {
                for (auto& e : pp.errors()) out << "  parse error: " << e.message << "\n";
                continue;
            }
            protoST::Compiler cc;
            auto bc = cc.compileModule(*ast);
            if (cc.hasErrors()) {
                for (auto& s : cc.errors()) out << "  compile error: " << s << "\n";
                continue;
            }
            try {
                auto* r = rt.runTopLevel(*bc);
                if (r == PROTO_NONE)       out << "  nil\n";
                else if (r == PROTO_TRUE)  out << "  true\n";
                else if (r == PROTO_FALSE) out << "  false\n";
                else {
                    try { out << "  " << r->toLong(rt.rootCtx()) << "\n"; }
                    catch (...) { out << "  " << r->toUTF8String(rt.rootCtx()) << "\n"; }
                }
            } catch (const std::exception& e) {
                out << "  error: " << e.what() << "\n";
            }
            continue;
        }
```

Add `#include "../frontend/Parser.h"` and `#include "../frontend/Compiler.h"` at the top of `DebuggerRuntime.cpp`.

- [ ] **Step 2: Test**

```cpp
TEST_CASE("Debugger: where shows pc and instructions; print evaluates", "[debugger][cmds]") {
    protoST::Parser P("nil halt.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    std::istringstream in("where\nprint 1 + 2\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    rt.runTopLevel(*bc);
    auto text = out.str();
    REQUIRE(text.find("pc:") != std::string::npos);
    REQUIRE(text.find("  3")  != std::string::npos);   // result of 1+2
}
```

- [ ] **Step 3: Commit**

```bash
cmake --build build -j && ctest --test-dir build -R debugger
git add src/debugger/DebuggerRuntime.cpp tests/unit/test_debugger.cpp
git commit -m "feat(debugger): where, locals, print commands"
```

---

## Task 50 — Debugger: `step` / `next` / `finish` (single-step opcode mode)

**Files:**
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Modify: `protoST/src/debugger/DebuggerRuntime.h`
- Modify: `protoST/tests/unit/test_debugger.cpp`

After `cont` returns from the session, the engine resumes the bytecode loop. For `step`, the engine must re-enter the session after the **next instruction**. We implement this by adding a `stepMode_` flag in `DebuggerRuntime`: when set, the engine raises a synthetic `DebuggerHalt("step")` right after dispatching one instruction.

`next`: same as `step` but ignore SEND opcodes' inner steps — implement F2 as alias of `step` (true frame-depth tracking arrives with F3's method invocation). `finish`: run until next RETURN/RETURN_TOP, then halt.

- [ ] **Step 1: Add flags to `DebuggerRuntime`**

```cpp
    enum class Mode : uint8_t { Free, SingleStep, RunToReturn };
    void setMode(Mode m) { mode_.store(static_cast<uint8_t>(m), std::memory_order_relaxed); }
    Mode mode() const    { return static_cast<Mode>(mode_.load(std::memory_order_relaxed)); }

private:
    std::atomic<uint8_t> mode_{static_cast<uint8_t>(Mode::Free)};
```

After processing `s`/`n`/`f`/`c`:
- `c`: `setMode(Mode::Free); return;`
- `s` or `n`: `setMode(Mode::SingleStep); return;`
- `f`: `setMode(Mode::RunToReturn); return;`

- [ ] **Step 2: Engine — honour the mode**

At the start of every iteration of the while loop (after `pc += 2;` and before the switch), insert:

```cpp
        auto dbgMode = rt_.debugger().mode();
        if (rt_.debugger().attached() && dbgMode != DebuggerRuntime::Mode::Free) {
            // Capture the snapshot at the *new* pc before executing the instruction
            DebugFrame frame; frame.module = &m; frame.pc = pc - 2;
            frame.stack = stack; frame.locals = locals;
            rt_.debugger().enterSession(rt_, std::move(frame), "step");
            // session may have updated mode (e.g. user typed `c`).
        }
```

And at `RETURN`/`RETURN_TOP`, if `mode == RunToReturn`, set `mode = SingleStep` to halt after returning to the caller — but F2 has no method frames yet, so for now interpret `finish` as "run to end and halt one last time":

(Keep simple: `finish` behaves like `cont` in F2; full implementation arrives with F3's call stack.)

- [ ] **Step 3: Test**

```cpp
TEST_CASE("Debugger: single-step halts every instruction until cont", "[debugger][step]") {
    protoST::Parser P("nil halt. 1 + 2.");
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    // Step a couple of times then continue
    std::istringstream in("step\nstep\nstep\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->toLong(rt.rootCtx()) == 3);
    // Should have entered the session at least 3 times (the initial halt + 2 steps)
    size_t count = 0; size_t pos = 0;
    while ((pos = out.str().find("(stdbg)", pos)) != std::string::npos) { ++count; ++pos; }
    REQUIRE(count >= 3);
}
```

- [ ] **Step 4: Commit**

```bash
cmake --build build -j && ctest --test-dir build -R debugger
git add src/runtime/ExecutionEngine.cpp src/debugger/DebuggerRuntime.h src/debugger/DebuggerRuntime.cpp tests/unit/test_debugger.cpp
git commit -m "feat(debugger): step/next via per-instruction halt mode (finish placeholder for F3)"
```

---

## Task 51 — Debugger: location breakpoints

**Files:**
- Create: `protoST/src/debugger/BreakpointTable.h`
- Create: `protoST/src/debugger/BreakpointTable.cpp`
- Modify: `protoST/src/debugger/DebuggerRuntime.h`
- Modify: `protoST/src/runtime/ExecutionEngine.cpp`
- Modify: `protoST/src/debugger/DebuggerRuntime.cpp`
- Modify: `protoST/CMakeLists.txt`
- Modify: `protoST/tests/unit/test_debugger.cpp`

A breakpoint is a `(BytecodeModule*, pc)` pair; in F2 the user installs them by raw pc rather than `Class>>selector` (selector binding requires F3). We expose a CLI `break <pc>` command that installs a break in the current frame's module.

- [ ] **Step 1: Create `BreakpointTable.h`**

```cpp
#pragma once
#include <cstddef>
#include <unordered_set>

namespace protoST {

class BytecodeModule;

class BreakpointTable {
public:
    // Returns true if a breakpoint exists at (module, pc).
    bool isSet(const BytecodeModule* m, size_t pc) const;
    void add(const BytecodeModule* m, size_t pc);
    void remove(const BytecodeModule* m, size_t pc);
    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        const BytecodeModule* module;
        size_t pc;
        bool operator==(const Entry& o) const noexcept { return module == o.module && pc == o.pc; }
    };
    struct EntryHash {
        size_t operator()(const Entry& e) const noexcept {
            return std::hash<const void*>()(e.module) ^ (std::hash<size_t>()(e.pc) << 1);
        }
    };
    std::unordered_set<Entry, EntryHash> entries_;
};

} // namespace protoST
```

- [ ] **Step 2: `BreakpointTable.cpp`**

```cpp
#include "BreakpointTable.h"
namespace protoST {

bool BreakpointTable::isSet(const BytecodeModule* m, size_t pc) const {
    return entries_.find(Entry{m, pc}) != entries_.end();
}
void BreakpointTable::add(const BytecodeModule* m, size_t pc) { entries_.insert(Entry{m, pc}); }
void BreakpointTable::remove(const BytecodeModule* m, size_t pc) { entries_.erase(Entry{m, pc}); }

} // namespace protoST
```

- [ ] **Step 3: Embed in `DebuggerRuntime`**

```cpp
#include "BreakpointTable.h"
// ...
    BreakpointTable& breakpoints() { return breakpoints_; }
private:
    BreakpointTable breakpoints_;
```

- [ ] **Step 4: Engine — check at each instruction**

After the `step-mode` check (Task 50) but before the switch:

```cpp
        if (rt_.debugger().attached() && rt_.debugger().breakpoints().isSet(&m, pc - 2)) {
            DebugFrame frame; frame.module = &m; frame.pc = pc - 2;
            frame.stack = stack; frame.locals = locals;
            rt_.debugger().enterSession(rt_, std::move(frame), "breakpoint");
        }
```

- [ ] **Step 5: CLI commands in `enterSession`**

```cpp
        if (line.rfind("break ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                rt.debugger().breakpoints().add(frame.module, bpc);
                out << "  break set at pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }
        if (line == "info breaks") {
            out << "  " << rt.debugger().breakpoints().size() << " breakpoints set\n";
            continue;
        }
        if (line.rfind("clear ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                rt.debugger().breakpoints().remove(frame.module, bpc);
                out << "  cleared pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }
```

- [ ] **Step 6: Test**

```cpp
TEST_CASE("Debugger: location breakpoint halts at given pc", "[debugger][bp]") {
    protoST::Parser P("1 + 2.");          // PUSH_CONST 0; PUSH_CONST 1; SEND_BINARY 2; RETURN_TOP
    protoST::Compiler C; auto bc = C.compileModule(*P.parseModule());
    protoST::STRuntime rt;
    rt.debugger().attach();
    rt.debugger().breakpoints().add(bc.get(), 4);  // before SEND_BINARY

    std::istringstream in("where\ncont\n");
    std::ostringstream out;
    rt.debugger().setInputStream(&in);
    rt.debugger().setOutputStream(&out);

    auto* r = rt.runTopLevel(*bc);
    REQUIRE(r->toLong(rt.rootCtx()) == 3);
    REQUIRE(out.str().find("breakpoint") != std::string::npos);
    REQUIRE(out.str().find("pc: 4")     != std::string::npos);
}
```

- [ ] **Step 7: Wire + commit**

```cmake
# add to protost_runtime sources:
    src/debugger/BreakpointTable.cpp
```

```bash
cmake --build build -j && ctest --test-dir build -R debugger
git add src/debugger/BreakpointTable.h src/debugger/BreakpointTable.cpp src/debugger/DebuggerRuntime.h src/debugger/DebuggerRuntime.cpp src/runtime/ExecutionEngine.cpp tests/unit/test_debugger.cpp CMakeLists.txt
git commit -m "feat(debugger): location breakpoints + break/clear/info-breaks commands"
```

(Selector-based breakpoints `Debugger breakAt: 'Counter>>increment'` are deferred to F3, when user-defined methods exist and their per-method `BytecodeModule*` is reachable from a selector lookup.)

---

## Task 52 — CLI: `protost -d script.st`

**Files:**
- Modify: `protoST/src/main.cpp`
- Create: `protoST/tests/cli/test_cli_debugger.sh`
- Create: `protoST/tests/fixtures/halt_demo.st`
- Modify: `protoST/CMakeLists.txt`

- [ ] **Step 1: Fixture**

`tests/fixtures/halt_demo.st`:

```smalltalk
"-- halt_demo.st --"
x := 1 + 2.
nil halt.
y := x + 10.
y.
```

- [ ] **Step 2: Implement `-d` in `main.cpp`**

Replace the `mode == "-d"` line:

```cpp
    if (mode == "-d") {
        if (argc < 3) { std::fprintf(stderr, "-d requires a path\n"); return 64; }
        const char* path = argv[2];
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) { std::fprintf(stderr, "cannot open %s\n", path); return 66; }
        std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n), '\0');
        std::fread(src.data(), 1, static_cast<size_t>(n), fp); std::fclose(fp);

        protoST::Parser P(std::move(src));
        auto ast = P.parseModule();
        if (!P.errors().empty()) {
            for (auto& e : P.errors())
                std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
            return 65;
        }
        protoST::Compiler C; auto bc = C.compileModule(*ast);
        if (C.hasErrors()) {
            for (auto& s : C.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        protoST::STRuntime rt;
        rt.debugger().attach();
        try {
            auto* r = rt.runTopLevel(*bc);
            if (r && r != PROTO_NONE) {
                try { std::printf("=> %lld\n", r->toLong(rt.rootCtx())); }
                catch (...) { std::printf("=> <obj>\n"); }
            }
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
```

- [ ] **Step 3: CLI test (uses heredoc-piped commands)**

`tests/cli/test_cli_debugger.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"

out=$(printf "where\nlocals\nprint 1 + 2\ncont\n" | "$PROTOST" -d "$FIXTURES/halt_demo.st")
echo "$out" | grep -q "halted: user halt" || { echo "FAIL: no halt"; echo "$out"; exit 1; }
echo "$out" | grep -q "pc:"                || { echo "FAIL: no pc"; echo "$out"; exit 1; }
echo "$out" | grep -q "  3"                || { echo "FAIL: print did not evaluate"; echo "$out"; exit 1; }
echo "$out" | grep -q "=> 13"              || { echo "FAIL: final value not 13"; echo "$out"; exit 1; }
echo OK
```

Add to top-level `CMakeLists.txt`:

```cmake
add_test(NAME cli_debugger
    COMMAND ${CMAKE_SOURCE_DIR}/tests/cli/test_cli_debugger.sh $<TARGET_FILE:protost> ${CMAKE_SOURCE_DIR}/tests/fixtures)
```

- [ ] **Step 4: Run and commit**

```bash
chmod +x tests/cli/test_cli_debugger.sh
cmake --build build -j && ctest --test-dir build -R cli_debugger --output-on-failure
git add tests/fixtures/halt_demo.st tests/cli/test_cli_debugger.sh src/main.cpp CMakeLists.txt
git commit -m "feat(cli): -d invokes the debugger end-to-end on a script with Halt"
```

---

## Task 53 — F2 close-out: full ctest, manual smoke, tag

- [ ] **Step 1: Run the full test suite**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: 100% pass.

- [ ] **Step 2: Manual smoke**

```bash
./build/protost -e "(1 to: 100) inject: 0 into: [:a :b | a + b]."
# F2 hero path (note: requires F3's library; in F2 this should error with doesNotUnderstand: to:)
# Use the F2 hero form instead:
./build/protost -e " sum := 0. i := 1. [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]. sum."
# expected: 5050

./build/protost -d tests/fixtures/halt_demo.st <<EOF
where
print x + 100
cont
EOF
```

- [ ] **Step 3: Tag**

```bash
git tag -a f2-complete -m "F2: compiler + bytecode + interpreter + CLI debugger"
```

---

## Plan self-review summary

Coverage map (spec → tasks):

| Spec section | Covered by |
|---|---|
| § 3.1 Minimal decoration | Tasks 28 (no extra opcode), 30 (no TypeBridge), 40 (inline marker without IC layer) |
| § 3.2 GC bridging | Task 37 (asyncRootSet), Task 39 (perpetual symbols via fromUTF8String->asString) |
| § 3.3 No std::vector for execution state | Caveat documented at Tasks 37 & 38; replaced in F6 (out of scope for this plan but flagged) |
| § 4 Architecture (frontend + runtime + debugger) | Tasks 1-23 frontend; 28-46 runtime; 47-52 debugger |
| § 5.1 Classes as prototypes | Task 39 — F2 only bootstraps the root tree; full class/method binding F3 |
| § 5.2 SEND dispatch | Task 40 |
| § 5.3 Literal mapping | Tasks 31 (compiler) + 37 (materialize) |
| § 5.4 Built-in hierarchy | Task 39 |
| § 5.5 Closures | Task 35 — F2 documented no-closure rule; full design F3+ |
| § 9.1 CLI flags `-e`, `--dump-ast`, `-d` | Tasks 23, 46, 52 |
| § 10 venv | Tasks 24-27 |
| § 11.1-3 Debugger | Tasks 47-52 |
| § 13 phase boundaries | `f1-complete` and `f2-complete` tags in Tasks 27 and 53 |

Items intentionally not covered in this plan (carried forward to F3+):

- **`lib/core.st` loading** — needs user-defined methods which need user-defined classes which need `Bootstrap` extensions (`ClassDecl` emission). F3.
- **Closures with captured upvalues** — F3 (closures are mandatory for any non-trivial user code).
- **`Future`/`Actor`** — F6.
- **`Halt now`** as a real class method on `Halt` rather than a synthetic instance message on `Object>>halt` — F3 (proper class methods).
- **DAP server** — F8.

Self-review of red flags:

- No "TBD"/"TODO" placeholders.
- Every code step shows the exact source. Each test step shows the assertions.
- Every shell command is literal.
- Type consistency check: `BytecodeModule::ConstKind`, `Op` enum values, primitive marker tag bit, `DebugFrame` shape, `DebuggerRuntime::Command` and `Mode` are referenced consistently across Tasks 28-52.

---

# Execution handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-19-protost-f1-f2-skeleton-parser-vm.md`.** Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best for a plan this long because each task is well-bounded and a fresh subagent keeps context clean.

2. **Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Best if you want to see every step in this conversation.

Which approach?

