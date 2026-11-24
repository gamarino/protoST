// Class instance templates: see InstanceTemplate.h.

#include "runtime/InstanceTemplate.h"

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "runtime/ListIndex.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <atomic>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace protoST {

namespace {

// The instance-layout epoch: advanced by every declaration of instance
// variables, anywhere in the process. A template records the epoch it was
// built in; one built in an earlier epoch is rebuilt. Process-wide rather
// than per runtime: a declaration in one runtime only makes the others
// rebuild a template once, which costs nothing a program could notice.
std::atomic<long long> g_layoutEpoch{1};

// Appends to `out` the "_iv_<name>" key of every name in `o`'s own
// `__instvar_names__` string not already in `seen`.
void addInstVarKeys(proto::ProtoContext* ctx, const Bootstrap& b, const proto::ProtoObject* o,
                    std::unordered_set<std::string>& seen,
                    std::vector<const proto::ProtoString*>& out) {
    const proto::ProtoObject* v = o->getOwnAttributeDirect(ctx, b.sym.instVarNames);
    const proto::ProtoString* str = (v && v != PROTO_NONE) ? v->asString(ctx) : nullptr;
    if (!str) return;
    std::string word;
    for (char c : str->toStdString(ctx) + " ") {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!word.empty() && seen.insert(word).second)
                out.push_back(proto::ProtoString::createSymbol(ctx, ("_iv_" + word).c_str()));
            word.clear();
        } else {
            word += c;
        }
    }
}

// A new template for `cls`: an immutable child whose shape names every
// instance variable of `cls` and of the classes on its chain, all unset.
const proto::ProtoObject* buildTemplate(STRuntime& rt, proto::ProtoContext* ctx,
                                        const proto::ProtoObject* cls) {
    const Bootstrap& b = rt.bootstrap();
    std::unordered_set<std::string> seen;
    std::vector<const proto::ProtoString*> keys;   // interned symbols: perennial
    addInstVarKeys(ctx, b, cls, seen, keys);
    if (const proto::ProtoList* chain = cls->getParents(ctx)) {
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i) {
            const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
            // A metaclass's names are class-side variables, slots of the
            // class object, never of an instance.
            if (p && p != PROTO_NONE && !isMetaclass(b, ctx, p))
                addInstVarKeys(ctx, b, p, seen, keys);
        }
    }
    const proto::ProtoObject* tmpl = cls->newChild(ctx, /*isMutable=*/false);
    if (keys.empty()) return tmpl;
    TransientPin pinTmpl(ctx, tmpl);
    // Give the shape every name, then unset every slot: removeAttribute
    // keeps the name in the shape and clears its value.
    std::vector<const proto::ProtoObject*> nils(keys.size(), PROTO_NONE);
    tmpl = tmpl->setAttributes(ctx, static_cast<unsigned>(keys.size()), keys.data(), nils.data());
    pinTmpl.reset(tmpl);
    for (const proto::ProtoString* k : keys) {
        tmpl = tmpl->removeAttribute(ctx, k);
        pinTmpl.reset(tmpl);
    }
    return tmpl;
}

} // namespace

void noteInstanceLayoutChanged() {
    g_layoutEpoch.fetch_add(1, std::memory_order_acq_rel);
}

long long instanceLayoutEpoch() {
    return g_layoutEpoch.load(std::memory_order_acquire);
}

const proto::ProtoObject* instanceTemplate(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* cls) {
    if (!cls || cls == PROTO_NONE) return nullptr;
    const Bootstrap& b = rt.bootstrap();
    // Read the epoch before the cache: a declaration that lands after this
    // load leaves a template marked with the older epoch, which the next
    // basicNew rebuilds.
    const long long epoch = g_layoutEpoch.load(std::memory_order_acquire);
    const proto::ProtoObject* t = cls->getOwnAttributeDirect(ctx, b.sym.instanceTemplate);
    if (t && t != PROTO_NONE) {
        const proto::ProtoObject* e = cls->getOwnAttributeDirect(ctx, b.sym.instanceTemplateEpoch);
        if (e && proto::isSmallInt(e) && proto::asSmallInt(e) == epoch
            && t->getFirstParent(ctx) == cls)
            return t;
    }
    // Only an object has attributes to template (newClone answers PROTO_NONE
    // for any other value without allocating). Classes always are objects;
    // this is the cold path, taken once per class and epoch.
    if (cls->newClone(ctx, false) == PROTO_NONE) return nullptr;
    t = buildTemplate(rt, ctx, cls);
    TransientPin pinT(ctx, t);
    // Cache it on the class in one publication. Two threads racing here
    // each publish a valid template; the later one stays. An immutable
    // receiver is not updated in place, so its template is rebuilt per call.
    const proto::ProtoString* names[2] = {b.sym.instanceTemplate, b.sym.instanceTemplateEpoch};
    const proto::ProtoObject* values[2] = {t, ctx->fromLong(epoch)};
    cls->setAttributes(ctx, 2, names, values);
    return t;
}

const proto::ProtoObject* newInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* cls) {
    if (const proto::ProtoObject* t = instanceTemplate(rt, ctx, cls))
        return t->newClone(ctx, /*isMutable=*/true);
    return cls->newChild(ctx, /*isMutable=*/true);
}

} // namespace protoST
