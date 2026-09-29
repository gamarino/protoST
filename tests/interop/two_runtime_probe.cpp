// Two-runtime probe: protoST and protoScala in ONE process.
//
// This is a reproducer, not a test registered with CTest. It links protoST's two
// static libraries and protoScala's shared library into one executable and reports,
// line by line, what crosses between the two runtimes and what does not. See
// tests/interop/README.md for how to build and run it and for the output measured
// on 2026-09-29.
//
// Modes:
//   two_runtime_probe import  <prog.st>
//       protoST imports a protoScala module through UMD. protoScala's providers
//       (`provider:scala`, `provider:compiled`) are appended to protoST's
//       resolution chain with STRuntime::addModuleProviderToChain, then <prog.st>
//       runs; it is expected to do `Import from: 'mathlib'`.
//
//   two_runtime_probe handoff <module.so> <prog.st>
//       Bypasses UMD. protoScala loads a protoscalac-compiled module in its own
//       space (Session::withModule), the probe prints what each runtime's context
//       reads from the objects the other runtime owns, binds the protoScala module
//       as the protoST global `ScalaMod`, and runs <prog.st>. It also loads the
//       protoST module `counter_lib` through `provider:st` from protoScala's
//       context, which is the path protoScala's `import st.counter_lib` takes.
#include "repl/Session.h"
#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string show(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return "<absent>";
    if (v->isString(ctx)) return "'" + v->asString(ctx)->toStdString(ctx) + "'";
    if (v->isMethod(ctx)) return "<protoCore method>";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%p", (const void*)v);
    return buf;
}

const proto::ProtoObject* attr(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                               const char* name) {
    return obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
}

int runST(protoST::STRuntime& st, const char* program) {
    try {
        st.loadModuleFromFile(st.rootCtx(), program, "probe");
    } catch (const std::exception& e) {
        std::printf("PROBE: protoST program failed: %s\n", e.what());
        std::fflush(stdout);
        return 1;
    }
    std::fflush(stdout);
    return 0;
}

struct Job {
    protoST::STRuntime* st;
    const char* program;
};

int handoffBody(proto::ProtoContext* scalaCtx, const proto::ProtoObject* scalaModule, void* ud) {
    Job& j = *static_cast<Job*>(ud);
    proto::ProtoContext* stCtx = j.st->rootCtx();

    // A protoScala object, read through each runtime's context.
    std::printf("PROBE: protoScala module %p\n", (const void*)scalaModule);
    std::printf("PROBE:   .add via protoScala ctx        = %s\n",
                show(scalaCtx, attr(scalaCtx, scalaModule, "add")).c_str());
    std::printf("PROBE:   .add via protoST ctx           = %s\n",
                show(stCtx, attr(stCtx, scalaModule, "add")).c_str());
    std::printf("PROBE:   .__class_name__ via protoST ctx = %s\n",
                show(stCtx, attr(stCtx, scalaModule, "__class_name__")).c_str());
    std::printf("PROBE:   symbol 'add' interned: protoScala %p, protoST %p\n",
                (const void*)proto::ProtoString::createSymbol(scalaCtx, "add"),
                (const void*)proto::ProtoString::createSymbol(stCtx, "add"));

    // A protoST object, obtained the way protoScala's `import st.counter_lib` does.
    proto::ModuleProvider* stp =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    const proto::ProtoObject* stModule = stp ? stp->tryLoad("counter_lib", scalaCtx) : nullptr;
    if (stModule && stModule != PROTO_NONE) {
        const proto::ProtoObject* counter = attr(scalaCtx, stModule, "Counter");
        std::printf("PROBE: protoST class Counter via provider:st from protoScala ctx = %s\n",
                    show(scalaCtx, counter).c_str());
        if (counter && counter != PROTO_NONE) {
            std::printf("PROBE:   .__class_name__ via protoScala ctx = %s\n",
                        show(scalaCtx, attr(scalaCtx, counter, "__class_name__")).c_str());
            std::printf("PROBE:   .__class_name__ via protoST ctx    = %s\n",
                        show(stCtx, attr(stCtx, counter, "__class_name__")).c_str());
            std::printf("PROBE:   .increment via protoScala ctx      = %s\n",
                        show(scalaCtx, attr(scalaCtx, counter, "increment")).c_str());
            std::printf("PROBE:   .increment via protoST ctx         = %s\n",
                        show(stCtx, attr(stCtx, counter, "increment")).c_str());
        }
    } else {
        std::printf("PROBE: provider:st did not load counter_lib (is it on STPATH?)\n");
    }
    std::fflush(stdout);

    j.st->globals()->setAttribute(
        stCtx, proto::ProtoString::createSymbol(stCtx, "ScalaMod"), scalaModule);
    return runST(*j.st, j.program);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "import") == 0) {
        protoST::STRuntime st;
        protoScala::Session session;
        st.addModuleProviderToChain("provider:scala");
        st.addModuleProviderToChain("provider:compiled");
        return runST(st, argv[2]);
    }
    if (argc >= 4 && std::strcmp(argv[1], "handoff") == 0) {
        protoST::STRuntime st;
        protoScala::Session session;
        Job j{&st, argv[3]};
        return session.withModule(argv[2], &handoffBody, &j);
    }
    std::fprintf(stderr,
                 "usage: two_runtime_probe import <prog.st>\n"
                 "       two_runtime_probe handoff <module.so> <prog.st>\n");
    return 2;
}
