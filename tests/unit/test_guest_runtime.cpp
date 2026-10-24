// A protoST runtime in its host's ProtoSpace: one ProtoSpace per process
// (protoCore docs/GLOBAL_MUTABLE_TABLE.md, "Recommended model").
//
// STRuntime(proto::ProtoSpace&) runs protoST in a space another runtime
// created. What must hold:
//   * no second space exists (ProtoSpace::liveSpaceCount);
//   * the host keeps what is one per space: its primitive prototype slots, its
//     error factory, its resolution chain (protoST only adds provider:st);
//   * protoST still answers its own sends to primitive values -- integers,
//     floats, strings, characters, nil -- through its own prototypes, although
//     the space's slots hold the host's;
//   * a module a host context imports through protoST's provider is protoST's
//     own object, not a facade over it, and survives a collection.
// The host here is a bare ProtoSpace with prototype slots of its own, which is
// what a host runtime (protoScala, for example) installs.

#include <catch2/catch_all.hpp>
#include "PosixEnv.h"

#include "protoST/STRuntime.h"
#include "frontend/Compiler.h"
#include "frontend/Parser.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

const proto::ProtoObject* runSource(protoST::STRuntime& rt,
                                    std::vector<std::unique_ptr<protoST::BytecodeModule>>& keep,
                                    const std::string& src) {
    protoST::Parser P(src);
    auto ast = P.parseModule();
    REQUIRE(P.errors().empty());
    protoST::Compiler C;
    auto bc = C.compileModule(*ast);
    REQUIRE(!C.hasErrors());
    const proto::ProtoObject* r = rt.runTopLevel(*bc);
    keep.push_back(std::move(bc));
    return r;
}

std::string text(protoST::STRuntime& rt, const proto::ProtoObject* v) {
    const proto::ProtoString* s = v ? v->asString(rt.rootCtx()) : nullptr;
    return s ? s->toStdString(rt.rootCtx()) : std::string("<not a string>");
}

long processId() {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace

TEST_CASE("Guest: a protoST runtime runs in its host's ProtoSpace", "[umd][guest]") {
    proto::ProtoSpace host;
    proto::ProtoContext hostCtx(&host, nullptr);
    // The host runtime's own prototypes for primitive values.
    proto::ProtoObject* hostInt = const_cast<proto::ProtoObject*>(host.objectPrototype->newChild(&hostCtx, true));
    proto::ProtoObject* hostString = const_cast<proto::ProtoObject*>(host.objectPrototype->newChild(&hostCtx, true));
    host.smallIntegerPrototype = hostInt;
    host.stringPrototype = hostString;
    proto::ProtoObject* const hostFloat = host.doublePrototype;
    proto::ProtoObject* const hostNone = host.nonePrototype;
    const proto::ProtoObject* hostChain = host.getResolutionChain();
#if defined(PROTOCORE_HAS_LIVE_SPACE_COUNT)
    const proto::proto_ulong spacesBefore = proto::ProtoSpace::liveSpaceCount();
#endif

    protoST::STRuntime st(host);
    std::vector<std::unique_ptr<protoST::BytecodeModule>> keep;

    SECTION("one space, and the host keeps its slots") {
        REQUIRE(st.space() == &host);
        REQUIRE_FALSE(st.ownsSpace());
#if defined(PROTOCORE_HAS_LIVE_SPACE_COUNT)
        REQUIRE(proto::ProtoSpace::liveSpaceCount() == spacesBefore);
#endif
        REQUIRE(host.smallIntegerPrototype == hostInt);
        REQUIRE(host.stringPrototype == hostString);
        REQUIRE(host.doublePrototype == hostFloat);
        REQUIRE(host.nonePrototype == hostNone);
        // The chain grew by protoST's provider; it was not replaced.
        const proto::ProtoList* chain = host.getResolutionChain()->asList(st.rootCtx());
        REQUIRE(chain != nullptr);
        bool hasST = false;
        for (proto::proto_ulong i = 0; i < chain->getSize(st.rootCtx()); ++i) {
            const proto::ProtoString* e = chain->getAt(st.rootCtx(), static_cast<int>(i))->asString(st.rootCtx());
            if (e && e->toStdString(st.rootCtx()) == "provider:st") hasST = true;
        }
        REQUIRE(hasST);
        (void)hostChain;
    }

    SECTION("protoST answers its sends to primitive values") {
        REQUIRE(text(st, runSource(st, keep, "(3 + 4) printString")) == "7");
        REQUIRE(runSource(st, keep, "'abcd' size")->asLong(st.rootCtx()) == 4);
        REQUIRE(runSource(st, keep, "$a value")->asLong(st.rootCtx()) == 97);
        REQUIRE(runSource(st, keep, "nil isNil") == PROTO_TRUE);
        REQUIRE(text(st, runSource(st, keep, "(1.5 + 1) printString")) == "2.5");
        REQUIRE(runSource(st, keep, "(30 factorial printString) size")->asLong(st.rootCtx()) == 33);
        REQUIRE(runSource(st, keep, "(3 respondsTo: #printString)") == PROTO_TRUE);
        REQUIRE(text(st, runSource(st, keep, "3 class name")) == "SmallInteger");
    }

    SECTION("a host context imports a protoST module: the same object, kept by a collection") {
        const std::string name = "guest_lib_" + std::to_string(processId());
        {
            std::ofstream f(name + ".st", std::ios::binary);
            f << "Object subclass: #GuestCounter instanceVariableNames: 'value'.\n"
              << "GuestCounter >> value ^ 42.\n";
        }
        ::setenv("STPATH", ".", 1);
        proto::ModuleProvider* provider =
            proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
        REQUIRE(provider != nullptr);
        const proto::ProtoObject* module = provider->tryLoad(name, &hostCtx);
        REQUIRE(module != nullptr);
        REQUIRE(module != PROTO_NONE);
        const proto::ProtoString* key = proto::ProtoString::createSymbol(&hostCtx, "GuestCounter");
        const proto::ProtoObject* viaHost = module->getAttribute(&hostCtx, key);
        const proto::ProtoObject* viaST = st.globals()->getAttribute(st.rootCtx(), key);
        REQUIRE(viaHost == viaST);

        const uint64_t before = host.getGCCycleCount();
        for (int round = 0; round < 50 && host.getGCCycleCount() <= before + 1; ++round) {
            host.triggerGC();
            for (int i = 0; i < 20000; ++i) (void)hostCtx.newList()->appendLast(&hostCtx, hostCtx.fromInteger(i));
            hostCtx.safepoint();
            std::this_thread::yield();
        }
        REQUIRE(host.getGCCycleCount() > before);
        REQUIRE(st.globals()->getAttribute(st.rootCtx(), key) == viaST);
        REQUIRE(runSource(st, keep, "GuestCounter new value")->asLong(st.rootCtx()) == 42);
        std::remove((name + ".st").c_str());
    }
}
