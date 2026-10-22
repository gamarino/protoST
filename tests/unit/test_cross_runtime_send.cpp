// A Smalltalk message sent from another runtime (modules/STForeignSend.h).
//
// A protoST method is an object carrying `__bc_ptr__`, not a proto::ProtoMethod,
// so another runtime that holds a protoST object (Track Y) could not run its
// methods. `__st_send__`, a protoCore method on Object, is the entry that can.
// As in test_cross_runtime_provider.cpp, what makes a caller foreign is a
// ProtoSpace protoST does not own, and a bare proto::ProtoSpace is exactly that:
// every call below is made with protoCore alone, from that space's context.
//
// What each case pins, and why it can fail:
//   * messages are SENDS: unary, keyword and class-side selectors reach their
//     methods, `new` runs `initialize`, an argument comes back as the same cell,
//     and class-side isolation and doesNotUnderstand behave as in a send;
//   * an Error nothing in protoST handles leaves as a proto::ProtoException
//     whose value is the Error itself, selected by its class;
//   * an exception raised by the caller's own callable crosses protoST
//     untouched -- the same C++ object, subclass extras included (E3) -- past a
//     handler for an unrelated class, and runs the `ensure:` it unwinds through;
//     `on: Exception do:` catches it and hands back the very value;
//   * a call from a thread other than protoST's owner is refused.

#include <catch2/catch_all.hpp>
#include "PosixEnv.h"

#include "protoST/STRuntime.h"
#include "modules/STModuleProvider.h"
#include "protoCore.h"

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

class SendModule {
public:
    SendModule() {
        static int serial = 0;
#if defined(_WIN32)
        const long pid = static_cast<long>(_getpid());
#else
        const long pid = static_cast<long>(::getpid());
#endif
        name_ = "cross_runtime_send_" + std::to_string(pid) + "_" + std::to_string(++serial);
        std::ofstream f(name_ + ".st", std::ios::binary);
        f << "\"-- " << name_ << ".st --\"\n"
          << "Object subclass: #Counter instanceVariableNames: 'value'.\n"
          << "Error subclass: #CounterOverflow.\n"
          << "Counter >> initialize\n"
          << "  value := 0.\n"
          << "\n"
          << "Counter >> increment\n"
          << "  value := value + 1.\n"
          << "  value > 2 ifTrue: [ ^ CounterOverflow new signal: 'counter overflow' ].\n"
          << "  ^ value.\n"
          << "\n"
          << "Counter >> value ^ value.\n"
          << "Counter >> at: k put: v ^ v.\n"
          << "Counter class >> boom ^ CounterOverflow new signal: 'boom'.\n"
          << "Counter >> guard: aCallable\n"
          << "  ^ [[ aCallable apply() ] on: ZeroDivide do: [:e | 'wrong handler']]\n"
          << "      ensure: [ value := value + 100 ].\n"
          << "\n"
          << "Counter >> rescue: aCallable\n"
          << "  ^ [ aCallable apply() ] on: Exception do: [:e | e ].\n";
    }
    ~SendModule() { std::remove((name_ + ".st").c_str()); }
    SendModule(const SendModule&) = delete;
    SendModule& operator=(const SendModule&) = delete;
    const std::string& name() const { return name_; }

private:
    std::string name_;
};

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* s) {
    return proto::ProtoString::createSymbol(ctx, s);
}

// What a foreign runtime writes: protoCore's array call form, nothing else.
const proto::ProtoObject* send(proto::ProtoContext* ctx, const proto::ProtoObject* recv,
                               const char* selector,
                               std::initializer_list<const proto::ProtoObject*> args = {}) {
    std::vector<const proto::ProtoObject*> a;
    a.push_back(reinterpret_cast<const proto::ProtoObject*>(sym(ctx, selector)));
    a.insert(a.end(), args.begin(), args.end());
    return recv->call(ctx, nullptr, sym(ctx, "__st_send__"), recv, a.data(),
                      static_cast<unsigned>(a.size()));
}

// An exception subclass of another runtime, carrying an extra that only that
// runtime reads (a traceback, say). E3: it must come back as the same object.
struct TracedException : proto::ProtoException {
    TracedException(proto::ProtoContext* ctx, const proto::ProtoObject* v, std::string t)
        : proto::ProtoException(ctx, v), trace(std::move(t)) {}
    std::string trace;
};

const proto::ProtoObject* g_thrownValue = nullptr;

// The foreign callable's body: raises a TracedException carrying g_thrownValue.
const proto::ProtoObject* raiseTraced(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoArguments*,
                                      const proto::ProtoKeywordArguments*) {
    throw TracedException(ctx, g_thrownValue, "at Foreign.raiseTraced (foreign.x:7)");
}

// A foreign callable: an object of the foreign space whose `apply` is a
// protoCore method, which is how protoScala's function values look to protoST.
const proto::ProtoObject* foreignCallable(proto::ProtoContext* ctx) {
    const proto::ProtoObject* f = ctx->newObject(true);
    f->setAttribute(ctx, sym(ctx, "apply"), ctx->fromMethod(nullptr, &raiseTraced));
    return f;
}

const proto::ProtoObject* g_counterClass = nullptr;
proto::ProtoContext* g_foreignCtx = nullptr;

// A foreign callable that sends back into protoST: `Counter boom`, an Error. It
// runs its own code in its own space, as a runtime called from protoST does
// (protoScala re-enters on its current context), so the inner send is a
// foreign one too.
const proto::ProtoObject* sendBoomBack(proto::ProtoContext*, const proto::ProtoObject*,
                                       const proto::ParentLink*, const proto::ProtoArguments*,
                                       const proto::ProtoKeywordArguments*) {
    return send(g_foreignCtx, g_counterClass, "boom");
}

}  // namespace

TEST_CASE("A foreign caller sends Smalltalk messages through __st_send__", "[umd][interop]") {
    const SendModule lib;
    protoST::STRuntime rt;
    proto::ModuleProvider* provider =
        proto::ProviderRegistry::instance().getProviderForSpec("provider:st");
    REQUIRE(provider != nullptr);

    proto::ProtoSpace foreign;
    REQUIRE(protoST::stRuntimeForSpace(&foreign) == nullptr);
    proto::ProtoContext ctx(&foreign, nullptr);

    const proto::ProtoObject* module = provider->tryLoad(lib.name(), &ctx);
    REQUIRE(module != PROTO_NONE);
    const proto::ProtoObject* counterClass = module->getAttribute(&ctx, sym(&ctx, "Counter"));
    REQUIRE(counterClass != PROTO_NONE);
    const proto::ProtoObject* overflowClass =
        module->getAttribute(&ctx, sym(&ctx, "CounterOverflow"));
    REQUIRE(overflowClass != PROTO_NONE);

    SECTION("the entry is a protoCore method every protoST object answers") {
        const proto::ProtoObject* entry = counterClass->getAttribute(&ctx, sym(&ctx, "__st_send__"));
        REQUIRE(entry != PROTO_NONE);
        CHECK(entry->isMethod(&ctx));
    }

    SECTION("new, unary and keyword messages run their methods") {
        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        REQUIRE(c != PROTO_NONE);
        CHECK(c->isInstanceOf(&ctx, counterClass) == PROTO_TRUE);
        CHECK(send(&ctx, c, "value")->asLong(&ctx) == 0);   // initialize ran
        CHECK(send(&ctx, c, "increment")->asLong(&ctx) == 1);
        CHECK(send(&ctx, c, "increment")->asLong(&ctx) == 2);
        CHECK(send(&ctx, c, "value")->asLong(&ctx) == 2);
        // An argument crosses as the same cell, and comes back as it.
        const proto::ProtoObject* probe = ctx.newObject(false);
        CHECK(send(&ctx, c, "at:put:", {ctx.fromInteger(1), probe}) == probe);
    }

    SECTION("an Error protoST does not handle leaves as a ProtoException carrying it") {
        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        send(&ctx, c, "increment");
        send(&ctx, c, "increment");
        bool caught = false;
        try {
            send(&ctx, c, "increment");
        } catch (const proto::ProtoException& e) {
            caught = true;
            CHECK(e.isInstanceOf(&ctx, overflowClass));
            const proto::ProtoObject* text = send(&ctx, e.value(), "messageText");
            REQUIRE(text->asString(&ctx) != nullptr);
            CHECK(text->asString(&ctx)->toStdString(&ctx) == "counter overflow");
        }
        CHECK(caught);
    }

    SECTION("a class-side method answers the class only, as in a send") {
        bool overflow = false;
        try { send(&ctx, counterClass, "boom"); }
        catch (const proto::ProtoException& e) { overflow = e.isInstanceOf(&ctx, overflowClass); }
        CHECK(overflow);

        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        const proto::ProtoObject* mnu =
            rt.globals()->getAttribute(rt.rootCtx(), sym(rt.rootCtx(), "MessageNotUnderstood"));
        REQUIRE(mnu != PROTO_NONE);
        bool notUnderstood = false;
        try { send(&ctx, c, "boom"); }
        catch (const proto::ProtoException& e) { notUnderstood = e.isInstanceOf(&ctx, mnu); }
        CHECK(notUnderstood);
    }

    SECTION("the caller's exception crosses protoST unchanged and runs ensure:") {
        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        g_thrownValue = ctx.newObject(false);
        const proto::ProtoObject* callable = foreignCallable(&ctx);
        bool caught = false;
        try {
            send(&ctx, c, "guard:", {callable});
        } catch (const TracedException& e) {
            caught = true;
            CHECK(e.value() == g_thrownValue);
            CHECK(e.trace == "at Foreign.raiseTraced (foreign.x:7)");
        } catch (const proto::ProtoException&) {
            FAIL("the exception came back, but not as the subclass that was thrown");
        }
        CHECK(caught);
        // ensure: ran during the unwind; the unrelated ZeroDivide handler did not.
        CHECK(send(&ctx, c, "value")->asLong(&ctx) == 100);
    }

    SECTION("on: Exception do: catches the caller's exception and answers the same value") {
        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        g_thrownValue = ctx.newObject(false);
        CHECK(send(&ctx, c, "rescue:", {foreignCallable(&ctx)}) == g_thrownValue);
    }

    SECTION("an Error raised in a send the caller makes from inside protoST reaches protoST's handler") {
        // protoST -> the caller's callable -> __st_send__ -> an Error. The handler
        // of `rescue:` is above the caller's frames; the inner send hides it, and
        // the Error must still reach it when the callable returns into protoST.
        g_counterClass = counterClass;
        g_foreignCtx = &ctx;
        const proto::ProtoObject* f = ctx.newObject(true);
        f->setAttribute(&ctx, sym(&ctx, "apply"), ctx.fromMethod(nullptr, &sendBoomBack));
        const proto::ProtoObject* c = send(&ctx, counterClass, "new");
        const proto::ProtoObject* caught = send(&ctx, c, "rescue:", {f});
        REQUIRE(caught != PROTO_NONE);
        CHECK(caught->isInstanceOf(&ctx, overflowClass) == PROTO_TRUE);
        CHECK(send(&ctx, caught, "messageText")->asString(&ctx)->toStdString(&ctx) == "boom");
    }

    SECTION("a send from another thread is refused") {
        std::string message;
        std::thread other([&] {
            proto::ProtoContext otherCtx(&foreign, nullptr);
            try {
                send(&otherCtx, counterClass, "new");
            } catch (const std::exception& e) {
                message = e.what();
            }
        });
        other.join();
        CHECK(message.find("thread that constructed the protoST runtime") != std::string::npos);
    }
}
