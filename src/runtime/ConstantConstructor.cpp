#include "runtime/ConstantConstructor.h"

#include "protoST/STRuntime.h"
#include "runtime/BlockCode.h"
#include "runtime/Bootstrap.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ClassedErrorSignal.h"
#include "runtime/ListIndex.h"
#include "runtime/InstanceTemplate.h"
#include "runtime/IvarModifiers.h"
#include "runtime/NativeError.h"
#include "runtime/NativeExceptionBridge.h"
#include "runtime/TransientPin.h"
#include "runtime/ValueFormat.h"
#include "protoCore.h"

#include <algorithm>
#include <memory>

namespace protoST {

namespace {

// The name a class object carries (`__class_name__`), or "" for none.
std::string classNameOf(const Bootstrap& b, proto::ProtoContext* ctx,
                        const proto::ProtoObject* cls) {
    const proto::ProtoObject* n = cls->getOwnAttributeDirect(ctx, b.sym.className);
    const proto::ProtoString* str = n && n != PROTO_NONE ? n->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

// The String or Symbol `o` records under `key` (own only), or "".
std::string ownText(proto::ProtoContext* ctx, const proto::ProtoObject* o,
                    const proto::ProtoString* key) {
    const proto::ProtoObject* v = o->getOwnAttributeDirect(ctx, key);
    const proto::ProtoString* str = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

// A class object (it owns `__class_name__`) that is not a metaclass.
bool isClass(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE || o->isInteger(ctx) || o->isFloat(ctx) || o->asString(ctx)
        || !proto::ProtoObject::isCellPointer(o))
        return false;
    const proto::ProtoObject* n = o->getOwnAttributeDirect(ctx, b.sym.className);
    return n && n != PROTO_NONE && !isMetaclass(b, ctx, o);
}

const proto::ProtoString* ivarKey(proto::ProtoContext* ctx, const std::string& name) {
    return proto::ProtoString::createSymbol(ctx, ("_iv_" + name).c_str());
}

// The keywords of a keyword selector, without their colons: `number:owner:`
// gives {number, owner}. Empty when the selector is not a keyword selector.
std::vector<std::string> keywordsOf(const std::string& selector) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : selector) {
        if (c != ':') { cur += c; continue; }
        if (cur.empty()) return {};
        out.push_back(cur);
        cur.clear();
    }
    if (!cur.empty()) return {};
    return out;
}

// The class's own constants, from its records, in source order.
std::vector<std::string> ownConstantsOf(proto::ProtoContext* ctx, const proto::ProtoObject* cls) {
    std::vector<std::string> out;
    for (const ModifiedIvar& e : ownModifiersOf(ctx, cls))
        if (e.modifier == IvarModifier::Constant) out.push_back(e.name);
    return out;
}

// The constructor selector the nearest ancestor of `cls` records, or "".
std::string inheritedConstructorOf(const Bootstrap& b, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* cls) {
    const proto::ProtoString* key = constantConstructorKey(ctx);
    std::string found;
    if (const proto::ProtoList* chain = cls->getParents(ctx)) {
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n && found.empty(); ++i) {
            const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
            if (isClass(b, ctx, p)) found = ownText(ctx, p, key);
        }
    }
    return found;
}

// What a class's constructor is: its selector over every constant (empty
// for none), and whether the class must own it (see the header).
struct ConstructorPlan {
    std::string selector;
    bool own = false;
};

ConstructorPlan planFor(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                        const std::vector<std::string>& ownConstants) {
    ConstructorPlan plan;
    const std::vector<std::string> constants = constantNamesOf(rt, ctx, cls, ownConstants);
    if (constants.empty()) return plan;
    plan.selector = constructorSelectorFor(constants);
    plan.own = !ownConstants.empty()
               || inheritedConstructorOf(rt.bootstrap(), ctx, cls) != plan.selector;
    return plan;
}

void refuseTakenSelector(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                         const ConstructorPlan& plan) {
    if (plan.selector.empty()) return;
    const Bootstrap& b = rt.bootstrap();
    const proto::ProtoObject* meta = cls->getOwnAttributeDirect(ctx, b.sym.metaclass);
    if (!meta || meta == PROTO_NONE) return;
    const proto::ProtoString* sel = proto::ProtoString::createSymbol(ctx, plan.selector.c_str());
    // The class's own record names the generated method; anything else the
    // metaclass holds under the selector is a user's method.
    if (meta->hasOwnAttribute(ctx, sel) != PROTO_TRUE
        || ownText(ctx, cls, constantConstructorKey(ctx)) == plan.selector)
        return;
    throw ClassedErrorSignal("Error",
        constructorRedefinitionText(plan.selector, classNameOf(b, ctx, cls)));
}

// ---------------------------------------------------------------------------
// The per-class cache (the Task 6 ruling on concern 5): one ProtoList under
// `__constant_cache__`,
//   [epoch, constantKeys, selector1, keys1, selector2, keys2, ...]
// where constantKeys are the `_iv_` keys of constantNamesOf(cls) in order, and
// each selectorN is a constructor the chain records that is still valid (its
// owner's constants still give it), with the keys its keywords name. Built
// once per class and instance-layout epoch: every declaration of instance
// variables advances the epoch, and so does addBehavior:, which copies a
// class's own attributes, this cache included.
// ---------------------------------------------------------------------------

const proto::ProtoString* cacheKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__constant_cache__");
}

const proto::ProtoObject* asObj(const proto::ProtoList* l) {
    return reinterpret_cast<const proto::ProtoObject*>(l);
}

const proto::ProtoList* keyListOf(proto::ProtoContext* ctx, const std::vector<std::string>& names,
                                  TransientPin& pin) {
    const proto::ProtoList* l = ctx->newList();
    pin.reset(asObj(l));
    for (const std::string& n : names) {
        l = l->appendLast(ctx, ivarKey(ctx, n)->asObject(ctx));
        pin.reset(asObj(l));
    }
    return l;
}

const proto::ProtoList* buildCache(STRuntime& rt, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* cls, long long epoch) {
    const Bootstrap& b = rt.bootstrap();
    const proto::ProtoString* recordKey = constantConstructorKey(ctx);
    TransientPin pinCache(ctx, nullptr), pinKeys(ctx, nullptr);
    const proto::ProtoList* cache = ctx->newList();
    pinCache.reset(asObj(cache));
    cache = cache->appendLast(ctx, ctx->fromLong(epoch));
    pinCache.reset(asObj(cache));
    cache = cache->appendLast(ctx, asObj(keyListOf(ctx, constantNamesOf(rt, ctx, cls), pinKeys)));
    pinCache.reset(asObj(cache));
    // The constructors the chain records, nearest first, as metaclass lookup
    // finds them: a selector a nearer class records hides a farther one.
    std::vector<std::string> seen;
    auto consider = [&](const proto::ProtoObject* k) {
        if (!isClass(b, ctx, k)) return;
        const std::string sel = ownText(ctx, k, recordKey);
        if (sel.empty() || std::find(seen.begin(), seen.end(), sel) != seen.end()) return;
        seen.push_back(sel);
        if (sel != constructorSelectorFor(constantNamesOf(rt, ctx, k))) return;   // stale
        // The Symbol as PUSH_CONST pushes it (ExecutionEngine, constSym), so
        // keysForConstructor compares identities.
        cache = cache->appendLast(ctx, reinterpret_cast<const proto::ProtoObject*>(
                                           proto::ProtoString::createSymbol(ctx, sel.c_str())));
        pinCache.reset(asObj(cache));
        cache = cache->appendLast(ctx, asObj(keyListOf(ctx, keywordsOf(sel), pinKeys)));
        pinCache.reset(asObj(cache));
    };
    forClassAndAncestors(ctx, cls, consider);
    return cache;
}

// The cache of `cls`, built when it has none from the current epoch.
const proto::ProtoList* constantCache(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* cls) {
    // Read the epoch before the cache, as instanceTemplate does: a
    // declaration landing after this load leaves a cache marked older.
    const long long epoch = instanceLayoutEpoch();
    const proto::ProtoString* key = cacheKey(ctx);
    const proto::ProtoObject* v = cls->getOwnAttributeDirect(ctx, key);
    if (const proto::ProtoList* l = v && v != PROTO_NONE ? v->asList(ctx) : nullptr) {
        const proto::ProtoObject* e = l->getSize(ctx) > 0 ? l->getAt(ctx, listIndex(0)) : nullptr;
        if (e && proto::isSmallInt(e) && proto::asSmallInt(e) == epoch) return l;
    }
    const proto::ProtoList* l = buildCache(rt, ctx, cls, epoch);
    TransientPin pin(ctx, asObj(l));
    // Two threads racing here each publish a valid cache; the later stays.
    cls->setAttribute(ctx, key, asObj(l));
    return l;
}

// The keys a valid constructor `selector` writes, or nullptr when the chain
// of `cls` records no valid constructor under it.
const proto::ProtoList* keysForConstructor(proto::ProtoContext* ctx, const proto::ProtoList* cache,
                                           const proto::ProtoObject* selector) {
    const proto::proto_ulong n = cache->getSize(ctx);
    for (proto::proto_ulong i = 2; i + 1 < n; i += 2)
        if (cache->getAt(ctx, listIndex(i)) == selector)
            return cache->getAt(ctx, listIndex(i + 1))->asList(ctx);
    return nullptr;
}

// The class whose own constructor record is `selector`, nearest first; `cls`
// itself when none is (cold: only for the stale-constructor error).
const proto::ProtoObject* recordOwnerOf(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* cls, const std::string& selector) {
    const proto::ProtoObject* owner = nullptr;
    const proto::ProtoString* recordKey = constantConstructorKey(ctx);
    forClassAndAncestors(ctx, cls, [&](const proto::ProtoObject* k) {
        if (!owner && isClass(rt.bootstrap(), ctx, k) && ownText(ctx, k, recordKey) == selector)
            owner = k;
    });
    return owner ? owner : cls;
}

} // namespace

const proto::ProtoString* constantConstructorKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__constant_constructor__");
}

std::string constantConstructorOf(STRuntime&, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls) {
    if (!cls || cls == PROTO_NONE || cls->isInteger(ctx) || cls->isFloat(ctx) || cls->asString(ctx))
        return {};
    const proto::ProtoObject* v = cls->getAttribute(ctx, constantConstructorKey(ctx));
    const proto::ProtoString* str = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
    return str ? str->toStdString(ctx) : std::string();
}

void checkConstantConstructorFree(STRuntime& rt, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls,
                                  const std::vector<std::string>& ownConstants) {
    if (!isClass(rt.bootstrap(), ctx, cls)) return;
    refuseTakenSelector(rt, ctx, cls, planFor(rt, ctx, cls, ownConstants));
}

void installConstantConstructor(STRuntime& rt, proto::ProtoContext* ctx,
                                const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    if (!isClass(b, ctx, cls)) return;
    const ConstructorPlan plan = planFor(rt, ctx, cls, ownConstantsOf(ctx, cls));
    const proto::ProtoString* key = constantConstructorKey(ctx);
    const std::string previous = ownText(ctx, cls, key);
    if (previous.empty() && !plan.own) return;   // every class that owns none
    refuseTakenSelector(rt, ctx, cls, plan);
    const proto::ProtoObject* meta = ensureMetaclass(b, ctx, cls);
    auto* mcls = const_cast<proto::ProtoObject*>(cls);

    // A constructor this class recorded that it no longer owns under that
    // selector: its constants changed (a redeclaration, or addBehavior:,
    // which copies the class's own records and class-side methods), or it
    // now inherits its constructor. M9 kept every user method off a recorded
    // selector, so the method there is the generated one.
    if (!previous.empty() && (previous != plan.selector || !plan.own)) {
        meta->removeAttribute(ctx, proto::ProtoString::createSymbol(ctx, previous.c_str()));
        mcls->removeAttribute(ctx, key);
    }
    if (!plan.own) return;
    const std::string& selector = plan.selector;
    const unsigned int n = static_cast<unsigned int>(keywordsOf(selector).size());

    // The body (plan decision D-d as amended by the Task 6 ruling on
    // concern 1; pre-flight ruling C11: local 0 is self, the arguments are
    // locals 1..n).
    auto module = std::make_unique<BytecodeModule>();
    module->setArgCount(static_cast<int>(n) + 1);
    const auto selIdx = static_cast<unsigned int>(module->internSymbol(selector));
    module->emit(Op::PUSH_SELF, 0);                       // receiver of the check
    module->emit(Op::PUSH_SELF, 0);                       // receiver of the construction
    for (unsigned int i = 1; i <= n; ++i) module->emitWide(Op::PUSH_LOCAL, i);
    module->emitWide(Op::MAKE_ARRAY, n);
    module->emitWide(Op::PUSH_CONST, selIdx);
    module->emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(
                                           module->internSymbol("__constructConstants:selector:")));
    module->emit(Op::DUP, 0);
    module->emitWide(Op::SEND_UNARY, static_cast<unsigned int>(module->internSymbol("initialize")));
    module->emit(Op::POP, 0);
    module->emitWide(Op::PUSH_CONST, selIdx);
    module->emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(
                                           module->internSymbol("__checkConstantsOf:after:")));
    module->emit(Op::RETURN, 0);
    std::vector<std::string> localNames{"self"};
    for (const std::string& k : keywordsOf(selector)) localNames.push_back(k);
    module->setLocalNames(std::move(localNames));
    const std::string className = classNameOf(b, ctx, cls);
    module->setDefiningClass(className);
    module->setDebugName(className + " class>>" + selector);
    module->setSourceName("<generated constructor>");
    const BytecodeModule& code = rt.adoptModule(std::move(module));

    // The method object, as PUSH_BLOCK makes one for a compiled method
    // (ExecutionEngine::newClosure): a clone of the closure template with
    // its four slots written at once. A method frame is its own home and
    // takes the receiver as self, so neither slot matters here; no captured
    // dictionary.
    const proto::ProtoObject* codeRef = blockCodeRefFor(rt, ctx, code);
    TransientPin pinCode(ctx, codeRef);
    const proto::ProtoString* names[4] = {
        b.sym.bcPtr, b.sym.captured, b.sym.homeFrame, b.sym.blockSelf };
    const proto::ProtoObject* values[4] = { codeRef, PROTO_NONE, ctx->fromLong(0), PROTO_NONE };
    const proto::ProtoObject* method = b.closureTemplate->setAttributes(ctx, 4, names, values);
    TransientPin pinMethod(ctx, method);

    const proto::ProtoString* selectorSym = proto::ProtoString::createSymbol(ctx, selector.c_str());
    meta->setAttribute(ctx, selectorSym, method);
    mcls->setAttribute(ctx, key, selectorSym->asObject(ctx));
}

const proto::ProtoObject* constructWithConstants(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* cls,
                                                 const proto::ProtoObject* args,
                                                 const proto::ProtoObject* selector) {
    const Bootstrap& b = rt.bootstrap();
    const proto::ProtoList* cache = constantCache(rt, ctx, cls);
    TransientPin pinCache(ctx, asObj(cache));
    const proto::ProtoList* keys = keysForConstructor(ctx, cache, selector);
    if (!keys) {
        // A constructor the chain no longer backs: a class it inherits from
        // was redeclared in place with other constants after the owner of
        // this constructor was declared (Task 6 ruling I2).
        const proto::ProtoString* selStr = selector ? selector->asString(ctx) : nullptr;
        const std::string sel = selStr ? selStr->toStdString(ctx) : std::string("?");
        const std::string owner = classNameOf(b, ctx, recordOwnerOf(rt, ctx, cls, sel));
        const proto::ProtoObject* v = signalErrorNamed(rt, ctx, "Error",
            "the generated constructor " + sel + " of " + owner
            + " is out of date: a class it inherits from changed its constants; redeclare "
            + owner);
        (void)v;
        return PROTO_NONE;
    }
    const proto::ProtoObject* dataObj =
        args && args != PROTO_NONE ? args->getAttribute(ctx, b.sym.data) : nullptr;
    const proto::ProtoList* values =
        dataObj && dataObj != PROTO_NONE ? dataObj->asList(ctx) : nullptr;
    const proto::proto_ulong n = keys->getSize(ctx);
    if (!values || values->getSize(ctx) != n)
        throw NativeError("__constructConstants:selector: expects an Array of "
                          + std::to_string(n) + " values");

    // The instance (as basicNew makes it), and one write of the given
    // constants on the fresh, unpublished object; never nil (§6.2): a nil
    // argument leaves its constant unset.
    const proto::ProtoObject* instance = newInstance(rt, ctx, cls);
    TransientPin pinInstance(ctx, instance);
    std::vector<const proto::ProtoString*> names;
    std::vector<const proto::ProtoObject*> given;
    names.reserve(n);
    given.reserve(n);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        const proto::ProtoObject* v = values->getAt(ctx, listIndex(i));
        if (!v || v == PROTO_NONE) continue;
        names.push_back(keys->getAt(ctx, listIndex(i))->asString(ctx));
        given.push_back(v);
    }
    if (!names.empty())
        instance->setAttributes(ctx, static_cast<unsigned>(names.size()), names.data(), given.data());
    return instance;
}

const proto::ProtoObject* checkConstantsSet(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* cls,
                                            const proto::ProtoObject* instance,
                                            const std::string& selector) {
    if (!instance || instance == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoList* cache = constantCache(rt, ctx, cls);
    TransientPin pinCache(ctx, asObj(cache));
    const proto::ProtoList* keys = cache->getAt(ctx, listIndex(1))->asList(ctx);
    const proto::proto_ulong n = keys->getSize(ctx);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        const proto::ProtoString* key = keys->getAt(ctx, listIndex(i))->asString(ctx);
        const proto::ProtoObject* v = instance->getOwnAttributeDirect(ctx, key);
        if (v && v != PROTO_NONE) continue;
        // M13 (spec §12), signalled in place (pre-flight ruling C1). Error is
        // not resumable; should a handler's answer ever come back here, the
        // construction still answers nothing (§6.2, Task 6 ruling m2).
        const std::string name = key->toStdString(ctx).substr(4);   // drop "_iv_"
        signalErrorNamed(rt, ctx, "Error",
            "the constant '" + name + "' of " + articleAndClassName(rt, ctx, instance)
            + " is not set at the end of construction (" + selector + ")");
        return PROTO_NONE;
    }
    return instance;
}

} // namespace protoST
