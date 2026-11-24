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

// ---------------------------------------------------------------------------
// The generated `new` (ConstantConstructor.h): a hidden parent of the
// metaclass.
// ---------------------------------------------------------------------------

const proto::ProtoString* generatedNewKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__constant_new__");
}

// Owned by a hidden parent only: the metaclass it was built for.
const proto::ProtoString* hiddenOwnerKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__generated_new_of__");
}

const proto::ProtoString* newSelector(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "new");
}

// The metaclass `cls` owns, or nullptr when it has none yet.
const proto::ProtoObject* ownMetaclassOf(const Bootstrap& b, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* cls) {
    const proto::ProtoObject* m = cls->getOwnAttributeDirect(ctx, b.sym.metaclass);
    return m && m != PROTO_NONE ? m : nullptr;
}

// The hidden parent `cls` recorded, when it was built for the metaclass the
// class owns now (addBehavior: and a class copy carry the record of the
// class they were built from, whose metaclass is another one), else nullptr.
const proto::ProtoObject* liveHiddenOf(const Bootstrap& b, proto::ProtoContext* ctx,
                                       const proto::ProtoObject* cls) {
    const proto::ProtoObject* h = cls->getOwnAttributeDirect(ctx, generatedNewKey(ctx));
    if (!h || h == PROTO_NONE) return nullptr;
    const proto::ProtoObject* meta = ownMetaclassOf(b, ctx, cls);
    return meta && h->getOwnAttributeDirect(ctx, hiddenOwnerKey(ctx)) == meta ? h : nullptr;
}

// Whether metaclass lookup from `cls`, past its own metaclass, reaches a
// generated new: the first class on its chain, nearest first, that either
// has a hidden parent (answers true) or is a kernel class whose metaclass
// holds a `new` itself (Object, SortedCollection, ...: answers false)
// decides. A `new` written by user code is transparent here: whether it
// reaches the check through `super new` is the user's choice, and a subclass
// inherits it (explicit-state spec §6.2, conformance 58 and 89).
bool inheritsGeneratedNew(const Bootstrap& b, proto::ProtoContext* ctx,
                          const proto::ProtoObject* cls) {
    const proto::ProtoList* chain = cls->getParents(ctx);
    if (!chain) return false;
    TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
    const proto::ProtoString* userDeclared = proto::ProtoString::createSymbol(ctx, "__user_declared__");
    const proto::proto_ulong n = chain->getSize(ctx);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
        if (!isClass(b, ctx, p)) continue;
        if (liveHiddenOf(b, ctx, p)) return true;
        if (p->getOwnAttributeDirect(ctx, userDeclared) == PROTO_TRUE) continue;
        const proto::ProtoObject* meta = ownMetaclassOf(b, ctx, p);
        if (!meta) continue;
        const proto::ProtoObject* own = meta->getOwnAttributeDirect(ctx, newSelector(ctx));
        if (own && own != PROTO_NONE) return false;
    }
    return false;
}

// The method `new  ^ self __checkConstantsOf: super new after: #new`,
// compiled by hand as the generated constructor is.
const proto::ProtoObject* buildGeneratedNewMethod(STRuntime& rt, proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    auto module = std::make_unique<BytecodeModule>();
    module->setArgCount(1);                                  // local 0 is self
    const auto newIdx = static_cast<unsigned int>(module->internSymbol("new"));
    module->emit(Op::PUSH_SELF, 0);                          // receiver of the check
    module->emit(Op::PUSH_SELF, 0);                          // receiver of super new
    module->emitWide(Op::SEND_SUPER, newIdx);
    module->emitWide(Op::PUSH_CONST, newIdx);
    module->emitWide(Op::SEND_KEYWORD, static_cast<unsigned int>(
                                           module->internSymbol("__checkConstantsOf:after:")));
    module->emit(Op::RETURN, 0);
    module->setLocalNames({"self"});
    const std::string className = classNameOf(b, ctx, cls);
    module->setDefiningClass(className);
    module->setDebugName(className + " class>>new");
    module->setSourceName("<generated new>");
    const BytecodeModule& code = rt.adoptModule(std::move(module));
    const proto::ProtoObject* codeRef = blockCodeRefFor(rt, ctx, code);
    TransientPin pinCode(ctx, codeRef);
    const proto::ProtoString* names[4] = {
        b.sym.bcPtr, b.sym.captured, b.sym.homeFrame, b.sym.blockSelf };
    const proto::ProtoObject* values[4] = { codeRef, PROTO_NONE, ctx->fromLong(0), PROTO_NONE };
    return b.closureTemplate->setAttributes(ctx, 4, names, values);
}

const proto::ProtoObject* asListObj(const proto::ProtoList* l) {
    return reinterpret_cast<const proto::ProtoObject*>(l);
}

bool isHiddenParent(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE) return false;
    const proto::ProtoObject* owner = o->getOwnAttributeDirect(ctx, hiddenOwnerKey(ctx));
    return owner && owner != PROTO_NONE;
}

// The parent list of `meta` without hidden parents, with the live hidden
// parent of every metaclass on it placed right after that metaclass (where
// a metaclass created now, as a child of it, would see it), and `ownHidden`
// first when given. Its parents become the rest of the list, so its
// `super new` continues where the metaclass's lookup would. The metaclass is
// re-parented only when the list changed.
void rebuildMetaclassChain(const Bootstrap& b, proto::ProtoContext* ctx,
                           const proto::ProtoObject* meta, const proto::ProtoObject* ownHidden) {
    const proto::ProtoList* cur = meta->getParents(ctx);
    TransientPin pinCur(ctx, asListObj(cur));
    std::vector<const proto::ProtoObject*> curV, out;
    const proto::proto_ulong n = cur ? cur->getSize(ctx) : 0;
    for (proto::proto_ulong i = 0; i < n; ++i) curV.push_back(cur->getAt(ctx, listIndex(i)));
    for (const proto::ProtoObject* e : curV) {
        if (isHiddenParent(ctx, e)) continue;
        out.push_back(e);
        if (isMetaclass(b, ctx, e)) {
            const proto::ProtoObject* cls = e->getOwnAttributeDirect(ctx, b.sym.soleInstance);
            if (cls && cls != PROTO_NONE)
                if (const proto::ProtoObject* h = liveHiddenOf(b, ctx, cls))
                    if (h != ownHidden) out.push_back(h);
        }
    }
    // Nothing is allocated when the chain is already right: this runs for
    // every class declaration (the kernel loads hundreds), and building the
    // lists unconditionally cost the 0.17.0 startup.
    std::vector<const proto::ProtoObject*> want;
    if (ownHidden) want.push_back(ownHidden);
    want.insert(want.end(), out.begin(), out.end());
    bool hiddenRight = true;
    if (ownHidden) {
        const proto::ProtoList* hp = ownHidden->getParents(ctx);
        const proto::proto_ulong hn = hp ? hp->getSize(ctx) : 0;
        hiddenRight = hn == out.size();
        for (proto::proto_ulong i = 0; hiddenRight && i < hn; ++i)
            hiddenRight = hp->getAt(ctx, listIndex(i)) == out[i];
    }
    if (want == curV && hiddenRight) return;
    if (ownHidden && !hiddenRight) {
        const proto::ProtoList* rest = ctx->newList();
        TransientPin pinRest(ctx, asListObj(rest));
        for (const proto::ProtoObject* e : out) {
            rest = rest->appendLast(ctx, e);
            pinRest.reset(asListObj(rest));
        }
        ownHidden->setParents(ctx, rest);   // mutable: in place
    }
    if (want == curV) return;
    const proto::ProtoList* all = ctx->newList();
    TransientPin pinAll(ctx, asListObj(all));
    for (const proto::ProtoObject* e : want) {
        all = all->appendLast(ctx, e);
        pinAll.reset(asListObj(all));
    }
    meta->setParents(ctx, all);             // mutable: in place
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
    installGeneratedNew(rt, ctx, cls);
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

std::vector<const proto::ProtoObject*> classesHolding(STRuntime& rt, proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    // The classes bound as globals, collected first: the callback must not
    // allocate. Every one is reachable from the globals.
    struct Sink { const Bootstrap* b; std::vector<const proto::ProtoObject*> all; };
    Sink sink{&b, {}};
    if (const proto::ProtoObject* g = rt.globals())
        g->processOwnAttributes(ctx, &sink,
            [](proto::ProtoContext* c, void* self, const proto::ProtoString*,
               const proto::ProtoObject* v) {
                auto* k = static_cast<Sink*>(self);
                if (!v || v == PROTO_NONE || v->isInteger(c) || v->isFloat(c) || v->asString(c))
                    return;
                const proto::ProtoObject* own = v->getOwnAttributeDirect(c, k->b->sym.className);
                if (own && own != PROTO_NONE) k->all.push_back(v);
            });
    std::vector<std::pair<proto::proto_ulong, const proto::ProtoObject*>> found;
    std::vector<const proto::ProtoObject*> seen{cls};
    for (const proto::ProtoObject* k : sink.all) {
        if (std::find(seen.begin(), seen.end(), k) != seen.end() || !isClass(b, ctx, k)) continue;
        seen.push_back(k);
        const proto::ProtoList* chain = k->getParents(ctx);
        if (!chain) continue;
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i)
            if (chain->getAt(ctx, listIndex(i)) == cls) {
                found.emplace_back(n, k);
                break;
            }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<const proto::ProtoObject*> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

void installGeneratedNew(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    if (!isClass(b, ctx, cls)) return;
    const std::vector<std::string> own = ownConstantsOf(ctx, cls);
    const bool hasConstants = !constantNamesOf(rt, ctx, cls, own).empty();
    const bool needed = hasConstants && (!own.empty() || !inheritsGeneratedNew(b, ctx, cls));
    auto* mcls = const_cast<proto::ProtoObject*>(cls);
    if (!needed) {
        // No hidden parent of its own (none needed, or the one inherited
        // through the metaclass chain is enough); only a recorded one is
        // ever taken out of the chain.
        if (cls->hasOwnAttribute(ctx, generatedNewKey(ctx)) == PROTO_TRUE)
            mcls->removeAttribute(ctx, generatedNewKey(ctx));
        // The chain still follows the ancestors' hidden parents.
        if (const proto::ProtoObject* meta = ownMetaclassOf(b, ctx, cls))
            rebuildMetaclassChain(b, ctx, meta, nullptr);
        return;
    }
    const proto::ProtoObject* meta = ensureMetaclass(b, ctx, cls);
    const proto::ProtoObject* hidden = liveHiddenOf(b, ctx, cls);
    if (!hidden) {
        // A new hidden parent: an unnamed mutable object holding only the
        // generated new and its owner, fully built before any chain holds
        // it. The class's record and the metaclass's chain keep it alive.
        const proto::ProtoObject* method = buildGeneratedNewMethod(rt, ctx, cls);
        TransientPin pinMethod(ctx, method);
        const proto::ProtoObject* h = b.classProto->newChild(ctx, /*isMutable=*/true);
        TransientPin pinHidden(ctx, h);
        h->setAttribute(ctx, hiddenOwnerKey(ctx), meta);
        // `super new` inside it continues above the hidden parent.
        stampDefiningClass(rt, ctx, h, method);
        h->setAttribute(ctx, newSelector(ctx), method);
        mcls->setAttribute(ctx, generatedNewKey(ctx), h);
        hidden = h;
    }
    rebuildMetaclassChain(b, ctx, meta, hidden);
}

const proto::ProtoObject* visibleHolder(proto::ProtoContext* ctx, const proto::ProtoObject* holder) {
    if (!isHiddenParent(ctx, holder)) return holder;
    return holder->getOwnAttributeDirect(ctx, hiddenOwnerKey(ctx));
}

void refreshDependentGeneratedNews(STRuntime& rt, proto::ProtoContext* ctx,
                                   const proto::ProtoObject* cls) {
    for (const proto::ProtoObject* k : classesHolding(rt, ctx, cls)) installGeneratedNew(rt, ctx, k);
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
