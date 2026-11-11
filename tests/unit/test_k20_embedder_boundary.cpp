// K20 — an unhandled protoST error leaving STRuntime as a C++ exception that
// outlives the runtime.
//
// The ordinary embedder pattern, `try { STRuntime rt; rt.runTopLevel(m); }
// catch (...)`, destroys the runtime -- and the ProtoSpace it owns -- while
// the exception unwinds. A proto::ProtoException pins its value in that
// space, and its destructor then ran ProtoRootSet::remove on freed memory:
// the process died with SIGSEGV (4b47115 and before). At the embedder
// boundary of a runtime that owns its space the exception is now a
// self-contained protoST::STUncaughtError. These cases crash, or fail to
// compile, on 4b47115.
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "protoST/STUncaughtError.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/UnhandledSTException.h"
#include "protoCore.h"

#include <fstream>
#include <memory>
#include <string>

namespace {
std::unique_ptr<protoST::BytecodeModule> compile(const char* src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    return bc;
}
} // namespace

TEST_CASE("K20: an unhandled error caught after the runtime is destroyed",
          "[k20][exceptions]") {
    auto bc = compile("Error subclass: #AppError. AppError signal: 'too much'.");
    std::string message, report;
    bool kind = false;
    try {
        protoST::STRuntime rt;
        rt.runTopLevel(*bc);
    } catch (const protoST::STUncaughtError& e) {
        message = e.what();
        report = e.report();
        kind = e.isKindOf("AppError") && e.isKindOf("Error") && !e.isKindOf("ZeroDivide");
    }
    REQUIRE(message == "AppError: too much");
    REQUIRE(report.rfind("error: AppError: too much", 0) == 0);
    REQUIRE(kind);
}

TEST_CASE("K20: isKindOf follows handler selection, mixins included",
          "[k20][exceptions]") {
    // Review round 3 (R3-L2): `on: R3Tagged do:` selects an exception whose
    // class uses R3Tagged, so STUncaughtError::isKindOf("R3Tagged") holds,
    // and classNames() lists the ancestors in resolution order.
    auto bc = compile("Object subclass: #R3Tagged. "
                      "Error subclass: #R3MixErr uses: { R3Tagged }. "
                      "R3MixErr signal: 'mixed'.");
    std::vector<std::string> classes;
    bool tagged = false, error = false;
    try {
        protoST::STRuntime rt;
        rt.runTopLevel(*bc);
    } catch (const protoST::STUncaughtError& e) {
        classes = e.classNames();
        tagged = e.isKindOf("R3Tagged");
        error = e.isKindOf("Error");
    }
    REQUIRE(!classes.empty());
    REQUIRE(classes.front() == "R3MixErr");
    REQUIRE(tagged);
    REQUIRE(error);
}

TEST_CASE("K20: the plain std::exception embedder pattern", "[k20][exceptions]") {
    auto bc = compile("Object zork.");
    std::string message;
    try {
        protoST::STRuntime rt;
        rt.runTopLevel(*bc);
    } catch (const std::exception& e) {
        message = e.what();
    }
    REQUIRE(message.find("doesNotUnderstand: zork") != std::string::npos);
}

TEST_CASE("K20: a protoCore error and a module load, outliving the runtime",
          "[k20][exceptions]") {
    auto bc = compile("1 / 0.");
    std::vector<std::string> classes;
    try {
        protoST::STRuntime rt;
        rt.runTopLevel(*bc, rt.rootCtx());
    } catch (const protoST::STUncaughtError& e) {
        classes = e.classNames();
    }
    REQUIRE(!classes.empty());
    REQUIRE(classes.front() == "ZeroDivide");

    const std::string path = "k20_failing_module.st";
    { std::ofstream(path) << "Object subclass: #K20M.\nK20M zork.\n"; }
    std::string message;
    try {
        protoST::STRuntime rt;
        rt.loadModuleFromFile(rt.rootCtx(), path, "k20_failing_module");
    } catch (const std::exception& e) {
        message = e.what();
    }
    std::remove(path.c_str());
    REQUIRE(message.find("doesNotUnderstand: zork") != std::string::npos);
}

TEST_CASE("K20: inside the runtime's scope, a host-space runtime keeps the ProtoException",
          "[k20][exceptions]") {
    // A runtime in a host's space: the space outlives it, and the exception
    // is the unified ProtoException other languages select by prototype.
    auto bc = compile("Error subclass: #HostErr. HostErr signal: 'x'.");
    proto::ProtoSpace space;
    bool proto = false;
    {
        protoST::STRuntime rt(space);
        try {
            rt.runTopLevel(*bc);
        } catch (const proto::ProtoException& e) {
            proto = dynamic_cast<const protoST::UnhandledSTException*>(&e) != nullptr;
        }
    }
    REQUIRE(proto);
}
