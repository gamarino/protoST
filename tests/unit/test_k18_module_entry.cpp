// K18 — a module import requested on another runtime's frame context must
// leave that context's slots alone.
//
// Written by the K15 re-review (2026-11-11). A host frame with four sentinel
// locals imports a protoST module whose top level walks a value nested 300
// deep with collect:. Before fix round 2 both loads ran protoST's engine on
// the frame context itself: the sentinels were overwritten and the frame's
// automaticLocalsCount grew from 4 to 65536. Two shapes: a frame in protoST's
// own space (STModuleProvider::tryLoad), and a host frame with protoST a guest
// in the host's space (loadForForeignCaller, shared space).
#include <catch2/catch_all.hpp>
#include "protoST/STRuntime.h"
#include "protoCore.h"
#include <cstdio>
#include <fstream>
#include <string>
#if defined(_WIN32)
#include <process.h>
#define K18_GETPID _getpid
#else
#include <unistd.h>
#define K18_GETPID ::getpid
#endif

namespace {
constexpr unsigned kFrameLocals = 4;

std::string writeModule(const char* tag) {
    std::string name = std::string("k18_mod_") + tag + "_" + std::to_string(static_cast<long>(K18_GETPID()));
    std::ofstream f(name + ".st", std::ios::binary);
    f << "Object subclass: #K18Walker.\n"
         "K18Walker >> wrap: n around: seed\n"
         "  | v | v := seed. n timesRepeat: [v := Array with: v]. ^ v.\n"
         "K18Walker >> walk: v\n"
         "  v isInteger ifTrue: [^ v + 1].\n"
         "  ^ v collect: [:each | self walk: each].\n"
         "K18Walker new walk: (K18Walker new wrap: 300 around: 1).\n";
    return name;
}

// A host frame: its own context with sentinel locals, importing the module.
void importFromFrame(proto::ProtoSpace* space, proto::ProtoContext* parent, const std::string& name,
                     bool& kept, bool& inPlace, bool& loaded) {
    proto::ProtoContext frame(space, parent, nullptr, nullptr, nullptr, nullptr, kFrameLocals);
    const proto::ProtoObject* sentinels[kFrameLocals];
    for (unsigned i = 0; i < kFrameLocals; ++i) {
        sentinels[i] = frame.newObject(false);
        frame.setAutomaticLocal(i, sentinels[i]);
    }
    const proto::ProtoObject** locals = frame.getAutomaticLocals();
    const proto::ProtoObject* w = space->getImportModule(&frame, name.c_str(), "exports");
    loaded = w && w != PROTO_NONE;
    inPlace = frame.getAutomaticLocals() == locals;
    kept = true;
    for (unsigned i = 0; i < kFrameLocals; ++i)
        if (frame.getAutomaticLocal(i) != sentinels[i]) kept = false;
    INFO("automaticLocalsCount after import = " << frame.getAutomaticLocalsCount());
    CHECK(frame.getAutomaticLocalsCount() == kFrameLocals);
}
}  // namespace

TEST_CASE("K18: a UMD import from a frame context in protoST's own space leaves its slots alone", "[umd][interop][pin]") {
    const std::string name = writeModule("own");
    protoST::STRuntime rt;
    bool kept = false, inPlace = false, loaded = false;
    importFromFrame(rt.space(), rt.rootCtx(), name, kept, inPlace, loaded);
    std::remove((name + ".st").c_str());
    CHECK(loaded);
    CHECK(kept);
    CHECK(inPlace);
}

TEST_CASE("K18: a UMD import from a host frame context (guest runtime) leaves its slots alone", "[umd][interop][pin]") {
    const std::string name = writeModule("guest");
    proto::ProtoSpace host;
    proto::ProtoContext hostCtx(&host, nullptr);
    host.smallIntegerPrototype = const_cast<proto::ProtoObject*>(host.objectPrototype->newChild(&hostCtx, true));
    host.stringPrototype = const_cast<proto::ProtoObject*>(host.objectPrototype->newChild(&hostCtx, true));
    bool kept = false, inPlace = false, loaded = false;
    {
        protoST::STRuntime st(host);
        importFromFrame(&host, &hostCtx, name, kept, inPlace, loaded);
    }
    std::remove((name + ".st").c_str());
    CHECK(loaded);
    CHECK(kept);
    CHECK(inPlace);
}
