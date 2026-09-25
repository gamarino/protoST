// Track Y — a cross-runtime import, from protoST's side of the boundary.
//
// `proto::ModuleProvider::tryLoad(logicalPath, ctx)` receives the CALLER's
// context, and in a process holding two runtimes that caller is in the OTHER
// runtime's ProtoSpace. protoST used to resolve its own runtime from
// `ctx->space`, which is a space it does not own in that case: the lookup
// missed and a module that was there was reported as absent. That was the
// Phase 6 finding recorded under protoScala's R5.
//
// The fix is in the provider, not in the kernel. A `ModuleProvider` is an
// object with its own state, so for a caller in a space protoST does not own it
// takes the runtime from its own state (`soleSTRuntime`), runs the load in
// protoST's OWN space — where protoST's prototypes, globals and interned
// symbols live — and uses `ctx` only to allocate the result in the caller's
// context.
//
// No protoScala, protoPython or protoJS is needed to test that: what makes a
// caller foreign is simply a ProtoSpace protoST does not own, and a bare
// `proto::ProtoSpace` is exactly that. Nothing here registers a fake provider;
// the provider under test is protoST's own.
//
// The two properties asserted, and why each can fail:
//
//   * the import RESOLVES for a foreign caller, and the module namespace reads
//     back with symbols interned in the CALLER's space. protoCore interns per
//     space (`ctx->space->symbolTable`), so a name long enough to need a heap
//     symbol has a different address in each space; a short name is embedded in
//     the pointer word and matches across spaces by accident. `Counter` needs a
//     heap symbol, so it fails if the namespace is not re-keyed.
//
//   * the VALUES are not copied. The class the foreign caller reads is the same
//     cell protoST's globals hold — same address, and the same `getHash`
//     computed from either side. Copy, marshal or proxy anything and this goes
//     red.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "modules/STModuleProvider.h"
#include "protoCore.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {

// A module whose only exported name is too long for protoCore to embed in a
// pointer word, which is the case that decides whether the namespace was
// re-keyed in the caller's space.
void writeCounterModule(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "\"-- cross_runtime_lib.st --\"\n"
      << "Object subclass: #Counter instanceVariableNames: 'value'.\n"
      << "Counter >> initialize value := 0.\n"
      << "Counter >> increment value := value + 1.\n"
      << "Counter >> value ^ value.\n";
}

}  // namespace

TEST_CASE("Track Y: the provider serves a caller in a ProtoSpace protoST does not own",
          "[umd][interop]") {
    writeCounterModule("cross_runtime_lib.st");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime rt;
    REQUIRE(protoST::soleSTRuntime() == &rt);

    proto::ModuleProvider* provider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    REQUIRE(provider != nullptr);

    // A ProtoSpace protoST does not own — what a co-resident runtime looks like
    // from here. It must not be registered with protoST's space registry.
    proto::ProtoSpace foreign;
    REQUIRE(protoST::stRuntimeForSpace(&foreign) == nullptr);
    proto::ProtoContext foreignCtx(&foreign, nullptr);

    const proto::ProtoObject* module =
        provider->tryLoad("cross_runtime_lib", &foreignCtx);
    REQUIRE(module != nullptr);
    REQUIRE(module != PROTO_NONE);

    SECTION("the namespace reads back with keys interned in the caller's space") {
        const proto::ProtoString* counterKey =
            proto::ProtoString::createSymbol(&foreignCtx, "Counter");
        REQUIRE(module->hasAttribute(&foreignCtx, counterKey) == PROTO_TRUE);

        // The same name interned in protoST's space is a DIFFERENT pointer —
        // that is the whole reason the namespace has to be rebuilt.
        proto::ProtoContext stCtx(rt.space(), rt.rootCtx());
        const proto::ProtoString* counterKeyInST =
            proto::ProtoString::createSymbol(&stCtx, "Counter");
        REQUIRE(counterKey != counterKeyInST);

        // A name the module does not define stays absent.
        REQUIRE(module->hasAttribute(
                    &foreignCtx,
                    proto::ProtoString::createSymbol(
                        &foreignCtx, "ANameThisModuleNeverDefines")) == PROTO_FALSE);
    }

    SECTION("the value is the same object on both sides — nothing is copied") {
        const proto::ProtoObject* viaForeign = module->getAttribute(
            &foreignCtx, proto::ProtoString::createSymbol(&foreignCtx, "Counter"));
        REQUIRE(viaForeign != nullptr);
        REQUIRE(viaForeign != PROTO_NONE);

        proto::ProtoContext stCtx(rt.space(), rt.rootCtx());
        const proto::ProtoObject* viaST = rt.globals()->getAttribute(
            &stCtx, proto::ProtoString::createSymbol(&stCtx, "Counter"));
        REQUIRE(viaST != nullptr);
        REQUIRE(viaST != PROTO_NONE);

        std::printf("NO-COPY PROOF (protoST side)\n");
        std::printf("  protoST space      = %p\n", (const void*)rt.space());
        std::printf("  foreign space      = %p\n", (const void*)&foreign);
        std::printf("  Counter via foreign = %p  getHash = %lu\n",
                    (const void*)viaForeign, viaForeign->getHash(&foreignCtx));
        std::printf("  Counter via protoST = %p  getHash = %lu\n",
                    (const void*)viaST, viaST->getHash(&stCtx));
        std::fflush(stdout);

        REQUIRE((const void*)rt.space() != (const void*)&foreign);
        REQUIRE(viaForeign == viaST);
        REQUIRE(viaForeign->getHash(&foreignCtx) == viaST->getHash(&stCtx));

        // And it is still a usable protoST class: the name protoST stamps on
        // every class reads back through protoST's own context.
        const proto::ProtoObject* name = viaForeign->getAttribute(
            &stCtx, proto::ProtoString::createSymbol(&stCtx, "__class_name__"));
        REQUIRE(name != nullptr);
        REQUIRE(name != PROTO_NONE);
        REQUIRE(name->asString(&stCtx)->toStdString(&stCtx) == "Counter");
    }
}

TEST_CASE("Track Y: a module a foreign caller asks for and protoST does not have is a plain miss",
          "[umd][interop]") {
    ::setenv("STPATH", ".", 1);
    protoST::STRuntime rt;
    proto::ModuleProvider* provider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    REQUIRE(provider != nullptr);

    proto::ProtoSpace foreign;
    proto::ProtoContext foreignCtx(&foreign, nullptr);
    // "Not my module" must stay a miss, never an error: protoCore's resolver
    // moves on to the next chain entry, and a provider that raised for "not
    // mine" would break the chain for every other provider.
    REQUIRE(provider->tryLoad("no_such_module_anywhere", &foreignCtx) == PROTO_NONE);
}

TEST_CASE("Track Y: a cross-runtime import from another thread is refused, not raced",
          "[umd][interop]") {
    writeCounterModule("cross_runtime_lib.st");
    ::setenv("STPATH", ".", 1);

    protoST::STRuntime rt;
    proto::ModuleProvider* provider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    REQUIRE(provider != nullptr);

    // A foreign caller has no context of its own in protoST's space, so the
    // load runs on protoST's root context — and only the thread that
    // constructed the runtime may allocate on it (D26). Any other thread gets a
    // message; it must never get a silent race.
    std::string message;
    bool threw = false;
    std::thread other([&] {
        proto::ProtoSpace foreign;
        proto::ProtoContext foreignCtx(&foreign, nullptr);
        try {
            provider->tryLoad("cross_runtime_lib", &foreignCtx);
        } catch (const std::exception& e) {
            threw = true;
            message = e.what();
        }
    });
    other.join();

    REQUIRE(threw);
    REQUIRE(message.find("thread that constructed the protoST runtime") !=
            std::string::npos);
}
