// umd_host.cpp -- runs a protoST script with native UMD providers registered.
//
// protoST loads modules through protoCore's Unified Module Discovery
// (`Import from: 'logical/path'`, ProtoSpace::getImportModule), walking the
// space's resolution chain. The `protost` binary puts only its own provider
// (`provider:st`) in that chain and ships no plug-in loader, so a module
// written in C++ -- a ModuleProvider compiled into a shared library -- is
// reachable only from a host that links the library, registers its provider
// and adds the provider to the chain (STRuntime::addModuleProviderToChain).
// This is that host, for the test suite: it links protoCore's example
// geometry module (protoCore/examples/exceptions/, shared library
// protocore_example_geometry), registers it, and then behaves like
// `protost [--print-last] <file.st>`, so tests/conformance/run_conformance.sh
// drives it unchanged.
//
// The geometry module raises its own error types (GeometryError,
// NegativeRadius, DegenerateTriangle) as proto::ProtoExceptions from a
// different shared library; tests/umd/*.st catch them by name.

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ValueFormat.h"
#include "runtime/UnhandledSTException.h"
#include "protoCore.h"

#include "geometry_module/GeometryModule.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    bool printLast = false;
    int argi = 1;
    if (argi < argc && std::strcmp(argv[argi], "--print-last") == 0) {
        printLast = true;
        ++argi;
    }
    if (argi >= argc) {
        std::fprintf(stderr, "usage: %s [--print-last] <file.st>\n", argv[0]);
        return 64;
    }
    const char* path = argv[argi];
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "file not found: %s\n", path); return 66; }
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    protoST::Parser parser(std::move(src));
    auto ast = parser.parseModule();
    for (auto& e : parser.errors())
        std::fprintf(stderr, "%s:%d:%d: %s\n", path, e.line, e.column, e.message.c_str());
    if (!parser.errors().empty()) return 65;

    // The provider is process-global; the chain entry belongs to the space.
    example::registerGeometryProvider();
    try {
        std::unique_ptr<protoST::BytecodeModule> bc;   // outlives rt (see main.cpp)
        protoST::STRuntime rt;
        rt.addModuleProviderToChain("provider:example_geometry");
        protoST::Compiler compiler;
        bc = compiler.compileModule(*ast);
        bc->setSourceName(path);
        if (compiler.hasErrors()) {
            for (auto& s : compiler.errors()) std::fprintf(stderr, "compile error: %s\n", s.c_str());
            return 70;
        }
        // Report an uncaught exception while `rt` -- whose space holds the
        // exception's value and its pin -- is alive.
        try {
            const proto::ProtoObject* r = rt.runTopLevel(*bc);
            if (printLast) std::puts(protoST::formatValue(rt, rt.rootCtx(), r).c_str());
            return 0;
        } catch (const proto::ProtoException& e) {
            std::fprintf(stderr, "%s\n", protoST::describeUncaught(rt.rootCtx(), e).c_str());
            return 1;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", protoST::describeUncaught(e).c_str());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "error: an unknown exception reached the top level\n");
        return 1;
    }
}
