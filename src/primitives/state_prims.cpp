#include "primitives/state_prims.h"

#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/IvarModifiers.h"
#include "runtime/NativeError.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <atomic>
#include <cstdio>
#include <string>

namespace protoST {

// Defined in block_prims.cpp.
extern const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                              const proto::ProtoObject* block,
                                              const proto::ProtoObject* const* args,
                                              int argc);

// ===========================================================================
// The state messages (explicit-state spec §6.4, §6.5, §13.1).
//
//   anObject compareAndSet: aName from: expected to: new   → Boolean
//   anObject update: aName with: aBlock                     → the installed value
//   anObject setInstVar: aName from: expected to: new       (deprecated alias)
//
// Every object understands them. They write the receiver's OWN attribute
// `_iv_<name>` (never the chain) with protoCore's attribute CAS, as the Atom
// primitives write `__atom_value__`. Before anything is read or written the
// name is checked (a String or Symbol the receiver declares, else M8) and
// then the variable's modifier, per §6.5 (M5). Errors are signalled in
// place (pre-flight ruling C1).
// ===========================================================================

namespace {

// D-f: how many update:with: blocks are running on this thread. A thread
// runs a block to its end before it runs anything else: a wait inside the
// block signals M7 instead of yielding, so the count never crosses an actor
// switch.
thread_local int tUpdateBlockDepth = 0;

// Counts one running update:with: block for as long as it is in scope: the
// destructor runs on every exit, a non-local return, a signal's unwinding
// and any other C++ exception included (plan review focus 3).
class UpdateBlockScope {
public:
    UpdateBlockScope() { ++tUpdateBlockDepth; }
    ~UpdateBlockScope() { --tUpdateBlockDepth; }
    UpdateBlockScope(const UpdateBlockScope&) = delete;
    UpdateBlockScope& operator=(const UpdateBlockScope&) = delete;
};

// D-i: W1 is printed once per process, whatever the threads and runtimes.
std::atomic<bool> gSetInstVarWarned{false};

constexpr const char* kSetInstVarWarning =
    "protoST: warning: setInstVar:from:to: is deprecated and is removed in protoST 0.18.0; "
    "use compareAndSet:from:to:";

// The receiver as the error texts name it: "an Account", or a class by its
// bare name (pre-flight ruling C7), nil as nil. Reads attributes only;
// never sends.
std::string receiverPhrase(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r) {
    if (!r || r == PROTO_NONE) return "nil";
    if (r && r != PROTO_NONE && !r->isInteger(ctx) && !r->asString(ctx)) {
        const proto::ProtoObject* own = r->getOwnAttributeDirect(ctx, rt.bootstrap().sym.className);
        if (own && own != PROTO_NONE)
            if (const proto::ProtoString* s = own->asString(ctx)) return s->toStdString(ctx);
    }
    return articleAndClassName(rt, ctx, r);
}

// The variable a state message names, once its checks have passed.
struct StateTarget {
    const proto::ProtoString* key = nullptr;   // `_iv_<name>`
    std::string name;
    IvarModifier modifier = IvarModifier::Plain;
};

// The checks every state message makes first (spec §6.4, §6.5, §12):
//   - the name is a String or Symbol that the receiver declares, else M8;
//   - the modifier allows the message: a constant takes neither message, a
//     write-once variable only compareAndSet: from nil (`fromNil`), else M5.
// Answers true when the message may proceed. Otherwise the error has been
// signalled in place, and `answer` holds what the signal answered.
bool checkStateTarget(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r,
                      const proto::ProtoObject* nameArg, const char* selector, bool isUpdate,
                      bool fromNil, StateTarget& out, const proto::ProtoObject*& answer) {
    const proto::ProtoString* nameStr =
        nameArg && nameArg != PROTO_NONE ? nameArg->asString(ctx) : nullptr;
    if (!nameStr) {
        answer = signalErrorNamed(rt, ctx, "Error",
            std::string(selector) + " expects a String or Symbol, not "
            + formatValue(rt, ctx, nameArg ? nameArg : PROTO_NONE));
        return false;
    }
    out.name = nameStr->toStdString(ctx);
    bool declared = false;
    out.modifier = receiverModifierOf(rt, ctx, r, out.name, &declared);
    if (!declared) {
        // M8
        answer = signalErrorNamed(rt, ctx, "Error",
            receiverPhrase(rt, ctx, r) + " has no instance variable '" + out.name + "'");
        return false;
    }
    const bool refused =
        out.modifier == IvarModifier::Constant
        || (out.modifier == IvarModifier::WriteOnce && (isUpdate || !fromNil));
    if (refused) {
        // M5
        const bool constant = out.modifier == IvarModifier::Constant;
        answer = signalErrorNamed(rt, ctx, "ModificationForbidden",
            std::string(selector) + " cannot write the "
            + (constant ? "constant" : "write-once variable") + " '" + out.name + "' of "
            + receiverPhrase(rt, ctx, r) + "; "
            + (constant ? "assign it in initialize" : "assign it with :="));
        return false;
    }
    out.key = proto::ProtoString::createSymbol(ctx, ("_iv_" + out.name).c_str());
    return true;
}

// The refusal for a receiver protoCore cannot write (an immutable object):
// ModificationForbidden, never a false answer (spec §6.5, Task 7 brief).
const proto::ProtoObject* signalImmutableReceiver(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* r,
                                                  const char* selector, const StateTarget& t) {
    return signalErrorNamed(rt, ctx, "ModificationForbidden",
        std::string(selector) + " cannot write the instance variable '" + t.name + "' of "
        + receiverPhrase(rt, ctx, r) + ": the receiver is immutable; nothing was written");
}

// protoCore refuses every CAS on an immutable receiver, and gives no
// public way to ask whether a receiver is one. A CAS that fails while the
// variable still reads as the expected value is therefore unexplained: on a
// mutable receiver it means other writers changed the value and restored it
// between the CAS and the read, which a retry outlives; on an immutable one
// it repeats for ever. After this many consecutive unexplained failures the
// receiver is taken to be immutable. On a mutable receiver that would need
// as many complete change-and-restore cycles by other writers, each inside
// the window between one CAS and the next read.
constexpr unsigned kUnexplainedCasLimit = 1024;

enum class CasOutcome { Installed, Lost, Immutable };

// The CAS of the variable `key` from `expected` to `newValue`, on the
// receiver's OWN attribute. An expected nil matches unset (plan decision
// D-c): absent first, then an explicit nil, as prim_Atom_cas does.
CasOutcome casVariable(proto::ProtoContext* ctx, const proto::ProtoObject* r,
                       const proto::ProtoString* key, const proto::ProtoObject* expected,
                       const proto::ProtoObject* newValue) {
    auto* obj = const_cast<proto::ProtoObject*>(r);
    for (unsigned unexplained = 0;;) {
        const bool ok = expected == PROTO_NONE
            ? (obj->setAttributeIfEqual(ctx, key, nullptr, newValue)
               || obj->setAttributeIfEqual(ctx, key, PROTO_NONE, newValue))
            : obj->setAttributeIfEqual(ctx, key, expected, newValue);
        if (ok) return CasOutcome::Installed;
        const proto::ProtoObject* cur = r->getOwnAttributeDirect(ctx, key);
        if ((cur ? cur : PROTO_NONE) != expected) return CasOutcome::Lost;
        if (++unexplained == kUnexplainedCasLimit) return CasOutcome::Immutable;
    }
}

// compareAndSet:from:to: and its alias, under the selector that was sent.
const proto::ProtoObject* compareAndSet(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* r,
                                        const proto::ProtoObject* const* a, const char* selector) {
    const proto::ProtoObject* expected = a[1] ? a[1] : PROTO_NONE;
    const proto::ProtoObject* newValue = a[2] ? a[2] : PROTO_NONE;
    StateTarget t;
    const proto::ProtoObject* answer = PROTO_NONE;
    if (!checkStateTarget(rt, ctx, r, a[0], selector, /*isUpdate=*/false,
                          /*fromNil=*/expected == PROTO_NONE, t, answer))
        return answer;
    // Plan ruling G2: from nil to nil on a write-once variable writes
    // nothing and answers whether the variable is unset.
    if (t.modifier == IvarModifier::WriteOnce && newValue == PROTO_NONE) {
        const proto::ProtoObject* cur = r->getOwnAttributeDirect(ctx, t.key);
        return (!cur || cur == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
    }
    // On a write-once variable this is the "first wins, the others adopt it"
    // form (§6.3): it never compares values, it answers whether it won.
    switch (casVariable(ctx, r, t.key, expected, newValue)) {
        case CasOutcome::Installed: return PROTO_TRUE;
        case CasOutcome::Lost:      return PROTO_FALSE;
        case CasOutcome::Immutable: break;
    }
    return signalImmutableReceiver(rt, ctx, r, selector, t);
}

// anObject compareAndSet: aName from: expected to: new
//   Installs `new` if the variable holds `expected` (identity; nil matches
//   unset) and answers whether it did (spec §6.4).
const proto::ProtoObject* prim_Object_compareAndSet(STRuntime& rt, proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* r,
                                                    const proto::ProtoObject* const* a, int argc) {
    if (argc != 3) throw NativeError("compareAndSet:from:to: expects 3 arguments");
    return compareAndSet(rt, ctx, r, a, "compareAndSet:from:to:");
}

// anObject setInstVar: aName from: expected to: new   (deprecated, §13.1)
//   compareAndSet:from:to:, with its checks, under its own selector. The
//   first call of the process prints warning W1 on stderr.
const proto::ProtoObject* prim_Object_setInstVarFromTo(STRuntime& rt, proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* r,
                                                       const proto::ProtoObject* const* a,
                                                       int argc) {
    if (!gSetInstVarWarned.exchange(true, std::memory_order_relaxed)) {
        std::fputs(kSetInstVarWarning, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
    if (argc != 3) throw NativeError("setInstVar:from:to: expects 3 arguments");
    return compareAndSet(rt, ctx, r, a, "setInstVar:from:to:");
}

// anObject update: aName with: aBlock
//   Atom >> swap: applied to an instance variable (spec §6.4): reads the
//   variable (nil when unset), evaluates `aBlock value: old`, and installs
//   the result by CAS against the value read (identity). On a lost race it
//   starts again from the read, so the block may run several times and must
//   be pure. Answers the installed value. A non-local return or an exception
//   out of the block abandons the update, and nothing is written. While the
//   block runs, a wait on this thread signals M7 (D-f).
const proto::ProtoObject* prim_Object_updateWith(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* r,
                                                 const proto::ProtoObject* const* a, int argc) {
    static const char* const kSelector = "update:with:";
    if (argc != 2) throw NativeError("update:with: expects 2 arguments");
    StateTarget t;
    const proto::ProtoObject* answer = PROTO_NONE;
    if (!checkStateTarget(rt, ctx, r, a[0], kSelector, /*isUpdate=*/true,
                          /*fromNil=*/false, t, answer))
        return answer;
    const proto::ProtoObject* block = a[1];
    auto* obj = const_cast<proto::ProtoObject*>(r);
    for (;;) {
        // The raw own value: nullptr while the variable is absent, so the CAS
        // expects exactly what was read (as prim_Atom_swap, S39).
        const proto::ProtoObject* current = r->getOwnAttributeDirect(ctx, t.key);
        const proto::ProtoObject* old = current ? current : PROTO_NONE;
        // `old` is held across the block (arbitrary allocation) and the CAS;
        // `neu` across the CAS.
        TransientPin pinOld(ctx, old);
        const proto::ProtoObject* neu = nullptr;
        {
            UpdateBlockScope inBlock;
            const proto::ProtoObject* args[] = { old };
            neu = invokeBlock(rt, ctx, block, args, 1);
        }
        if (!neu) neu = PROTO_NONE;
        TransientPin pinNeu(ctx, neu);
        for (unsigned unexplained = 0;;) {
            if (obj->setAttributeIfEqual(ctx, t.key, current, neu)) return neu;
            // Lost to another writer: run the block again on the new value.
            if (r->getOwnAttributeDirect(ctx, t.key) != current) break;
            // Unexplained (see kUnexplainedCasLimit): the value the block was
            // given is still current, so its answer still applies.
            if (++unexplained == kUnexplainedCasLimit)
                return signalImmutableReceiver(rt, ctx, r, kSelector, t);
        }
    }
}

} // anon

bool insideUpdateBlock() { return tUpdateBlockDepth > 0; }

void installStatePrimitives(STRuntime& rt) {
    auto& b = rt.bootstrap();
    bindPrimitive(rt, b.objectProto, "compareAndSet:from:to:",
                  primitiveMethod<prim_Object_compareAndSet>);
    bindPrimitive(rt, b.objectProto, "update:with:",
                  primitiveMethod<prim_Object_updateWith>);
    bindPrimitive(rt, b.objectProto, "setInstVar:from:to:",
                  primitiveMethod<prim_Object_setInstVarFromTo>);
}

} // namespace protoST
