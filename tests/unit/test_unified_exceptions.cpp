// protoCore 3.0.0 unified exceptions: the C++ side of the contract.
//
// Every language on protoCore throws proto::ProtoException carrying the thrown
// value, and selects handlers by the value's prototype chain (protoCore
// headers/protoCore.h, ProtoException; EMBEDDER-CONFORMANCE.md rule 14). These
// cases check protoST's half from C++, the way an embedder or another runtime
// sees it:
//   * a ProtoException raised by a native method (a module's error type) is
//     caught by `on:do:` naming the type, with the very value raised, and a
//     handler for an unrelated type lets the SAME exception leave the runtime;
//   * an unhandled Smalltalk Error leaves the runtime as a ProtoException
//     carrying the Error instance;
//   * protoCore's own errors are built by protoST's error factory as protoST
//     classes.
// The language-level test with a real module in its own shared library is
// tests/umd/geometry_errors.st.
//
// This file also registers the Catch2 translator for ProtoException, which is
// not a std::exception: REQUIRE_THROWS_WITH in every test file of the binary
// reads an UnhandledSTException's message through it.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/Bootstrap.h"
#include "runtime/UnhandledSTException.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

CATCH_TRANSLATE_EXCEPTION(const proto::ProtoException& e) {
    if (const auto* u = dynamic_cast<const protoST::UnhandledSTException*>(&e))
        return u->message();
    return std::string("proto::ProtoException (a foreign value)");
}

namespace {

struct Host {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    proto::ProtoContext* ctx() { return rt.rootCtx(); }

    const proto::ProtoObject* run(const char* src) {
        protoST::Parser P(src);
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        kept.push_back(std::move(bc));
        return rt.runTopLevel(*kept.back());
    }

    void defineGlobal(const char* name, const proto::ProtoObject* value) {
        rt.globals()->setAttribute(ctx(), proto::ProtoString::createSymbol(ctx(), name), value);
    }
};

// The value the native method raises; the cases run one at a time.
const proto::ProtoObject* g_raised = nullptr;

int g_raises = 0;

const proto::ProtoObject* raiseModuleError(proto::ProtoContext* context, const proto::ProtoObject*,
                                           const proto::ParentLink*, const proto::ProtoArguments*,
                                           const proto::ProtoKeywordArguments*) {
    ++g_raises;
    context->raise(g_raised);
}

// A module, as a C++ UMD provider builds one: an error hierarchy
// (ModuleError > NotFound) and a method raising an instance of NotFound.
// Published as globals so the Smalltalk source can name them.
void defineModule(Host& h) {
    proto::ProtoContext* ctx = h.ctx();
    const proto::ProtoObject* moduleError = ctx->newObject(false);
    const proto::ProtoObject* notFound = moduleError->newChild(ctx);
    const proto::ProtoObject* unrelated = ctx->newObject(false);
    const proto::ProtoObject* module = ctx->newObject(false)->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "raise"),
        ctx->fromMethod(nullptr, &raiseModuleError));
    h.defineGlobal("ModuleError", moduleError);
    h.defineGlobal("NotFound", notFound);
    h.defineGlobal("Unrelated", unrelated);
    h.defineGlobal("Module", module);
    g_raised = notFound->newChild(ctx);
    g_raises = 0;
    h.defineGlobal("TheRaisedValue", g_raised);   // keeps it reachable
}

} // namespace

TEST_CASE("unified exceptions: on:do: naming a module's base type catches the raised value",
          "[exceptions][unified]") {
    Host h;
    defineModule(h);
    const proto::ProtoObject* caught =
        h.run("[Module raise()] on: ModuleError do: [:e | e].");
    REQUIRE(caught == g_raised);
}

TEST_CASE("unified exceptions: an unrelated handler lets the same ProtoException leave",
          "[exceptions][unified]") {
    Host h;
    defineModule(h);
    bool escaped = false;
    try {
        h.run("[Module raise()] on: Unrelated do: [:e | 'wrong'].");
    } catch (const proto::ProtoException& e) {
        escaped = true;
        REQUIRE(e.value() == g_raised);
        REQUIRE(e.isInstanceOf(h.ctx(), h.rt.globals()->getAttribute(
            h.ctx(), proto::ProtoString::createSymbol(h.ctx(), "ModuleError"))));
        // The original exception, not a protoST wrapper of it.
        REQUIRE(dynamic_cast<const protoST::UnhandledSTException*>(&e) == nullptr);
    }
    REQUIRE(escaped);
}

TEST_CASE("unified exceptions: on: Error do: does not select a module's error type",
          "[exceptions][unified]") {
    Host h;
    defineModule(h);
    REQUIRE_THROWS_AS(h.run("[Module raise()] on: Error do: [:e | 'wrong']."),
                      proto::ProtoException);
}

TEST_CASE("unified exceptions: an unhandled Error leaves as a ProtoException carrying it",
          "[exceptions][unified]") {
    Host h;
    bool escaped = false;
    try {
        h.run("Error subclass: #AppError. AppError signal: 'too much'.");
    } catch (const proto::ProtoException& e) {
        escaped = true;
        REQUIRE(e.isInstanceOf(h.ctx(), h.rt.bootstrap().errorProto));
        const auto* u = dynamic_cast<const protoST::UnhandledSTException*>(&e);
        REQUIRE(u != nullptr);
        REQUIRE(u->message() == "AppError: too much");
        REQUIRE(protoST::describeUncaught(h.ctx(), e).rfind("error: AppError: too much", 0) == 0);
    }
    REQUIRE(escaped);
}

TEST_CASE("unified exceptions: protoCore's errors are built as protoST classes",
          "[exceptions][unified]") {
    Host h;
    proto::ProtoContext* ctx = h.ctx();
    bool zero = false;
    try {
        (void) ctx->fromInteger(7)->divide(ctx, ctx->fromInteger(0));
    } catch (const proto::ProtoException& e) {
        zero = e.isInstanceOf(ctx, h.rt.bootstrap().zeroDivideProto)
            && e.isInstanceOf(ctx, h.rt.bootstrap().arithmeticErrorProto);
    }
    REQUIRE(zero);

    bool invalid = false;
    try {
        (void) ctx->fromString("ten", 10);
    } catch (const proto::ProtoException& e) {
        const proto::ProtoObject* cls = h.rt.globals()->getAttribute(
            ctx, proto::ProtoString::createSymbol(ctx, "InvalidArgument"));
        invalid = e.isInstanceOf(ctx, cls) && e.isInstanceOf(ctx, h.rt.bootstrap().errorProto);
    }
    REQUIRE(invalid);
}

TEST_CASE("unified exceptions: a protoCore error inside a primitive is caught by its protoST class",
          "[exceptions][unified]") {
    Host h;
    // 1 // 0 and 1.0 reciprocal-style divisions raise protoCore's
    // DivisionByZero through the factory; ZeroDivide stays resumable.
    const proto::ProtoObject* r = h.run("[(10 // 0) + 1] on: ZeroDivide do: [:e | e resume: 5].");
    REQUIRE(r->asLong(h.ctx()) == 6);
}

TEST_CASE("unified exceptions: a module error raised in a handler is not offered to that handler",
          "[exceptions][unified]") {
    Host h;
    defineModule(h);
    // The handler raises the module's error again. While it runs its own
    // entry is disabled, so nothing handles the second error; it must leave
    // without the handler running for it once its block has ended and the
    // entry is enabled again -- which is what the native boundary of `do:`,
    // crossed on the way out, would do if it offered the error again. Each
    // run of the handler raises once more.
    REQUIRE_THROWS_AS(
        h.run("[#(1) do: [:x | Module raise()]] on: ModuleError do: [:e | Module raise()]."),
        proto::ProtoException);
    REQUIRE(g_raises == 2);
}
