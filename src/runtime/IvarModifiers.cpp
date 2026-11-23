#include "runtime/IvarModifiers.h"

#include "protoST/STRuntime.h"
#include "runtime/Bootstrap.h"
#include "runtime/NativeError.h"

#include <algorithm>

namespace protoST {

namespace {

const proto::ProtoString* instVarNamesKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__instvar_names__");
}

const proto::ProtoString* ivarModifiersKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__ivar_modifiers__");
}

// A value that may own declaration records: not nil, not a number, not a
// String or a Symbol.
bool isAttributeHolder(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    return o && o != PROTO_NONE && !o->isInteger(ctx) && !o->isFloat(ctx) && !o->asString(ctx);
}

// An object with a parent chain to walk: an attribute holder that is a cell
// (not an immediate value such as a Boolean or a Character).
bool isChainedObject(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    return isAttributeHolder(ctx, o) && proto::ProtoObject::isCellPointer(o);
}

// The space-separated words of a String `o` owns under `key`, in order.
std::vector<std::string> ownWords(proto::ProtoContext* ctx, const proto::ProtoObject* o,
                                  const proto::ProtoString* key) {
    std::vector<std::string> out;
    if (!isAttributeHolder(ctx, o) || o->hasOwnAttribute(ctx, key) != PROTO_TRUE) return out;
    const proto::ProtoObject* v = o->getOwnAttributeDirect(ctx, key);
    const proto::ProtoString* str = v && v != PROTO_NONE ? v->asString(ctx) : nullptr;
    if (!str) return out;
    return splitNames(str->toStdString(ctx));
}

// A class object (it owns `__class_name__`) that is not a metaclass.
bool isClassObject(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!isChainedObject(ctx, o)) return false;
    const proto::ProtoObject* n = o->getOwnAttributeDirect(ctx, b.sym.className);
    return n && n != PROTO_NONE && !isMetaclass(b, ctx, o);
}

void collectConstants(const Bootstrap& b, proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                      std::vector<std::string>& out, std::unordered_set<std::string>& seen) {
    // The direct parents, in resolution order: the primary superclass, then
    // each mixin. getParents is the flattened chain [primary, its ancestors…,
    // mixin0, its ancestors…, …]; a class that is an ancestor of a parent
    // already taken is that parent's, not a direct one.
    std::vector<const proto::ProtoObject*> directs;
    if (const proto::ProtoList* chain = cls->getParents(ctx)) {
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i) {
            const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
            if (!isClassObject(b, ctx, p)) continue;
            bool covered = false;
            for (const proto::ProtoObject* d : directs)
                if (d->hasParent(ctx, p) != 0) { covered = true; break; }
            if (!covered) directs.push_back(p);
        }
    }
    for (const proto::ProtoObject* d : directs) collectConstants(b, ctx, d, out, seen);
    for (const ModifiedIvar& e : ownModifiersOf(ctx, cls))
        if (e.modifier == IvarModifier::Constant && seen.insert(e.name).second) out.push_back(e.name);
}

} // namespace

void addDeclaredNames(proto::ProtoContext* ctx, const proto::ProtoObject* o,
                      const proto::ProtoString* key, std::unordered_set<std::string>& out) {
    for (auto& w : ownWords(ctx, o, key)) out.insert(std::move(w));
}

std::vector<ModifiedIvar> ownModifiersOf(proto::ProtoContext* ctx, const proto::ProtoObject* cls) {
    std::vector<ModifiedIvar> out;
    for (const std::string& w : ownWords(ctx, cls, ivarModifiersKey(ctx))) {
        const std::size_t colon = w.find(':');
        IvarModifier m = IvarModifier::Plain;
        if (colon == std::string::npos || colon + 1 == w.size()
            || !parseModifierName(std::string_view(w).substr(0, colon), m))
            throw NativeError("malformed instance-variable modifier record '" + w + "'");
        out.push_back(ModifiedIvar{w.substr(colon + 1), m});
    }
    return out;
}

IvarModifier modifierOf(STRuntime&, proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                        const std::string& name, bool* declared) {
    bool found = false;
    IvarModifier mod = IvarModifier::Plain;
    if (isChainedObject(ctx, obj)) {
        const proto::ProtoString* ivKey = instVarNamesKey(ctx);
        forClassAndAncestors(ctx, obj, [&](const proto::ProtoObject* k) {
            if (found) return;
            const std::vector<std::string> names = ownWords(ctx, k, ivKey);
            if (std::find(names.begin(), names.end(), name) == names.end()) return;
            found = true;
            mod = modifierIn(ownModifiersOf(ctx, k), name);
        });
    }
    if (declared) *declared = found;
    return mod;
}

bool declaresInstVar(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                     const std::string& name) {
    const Bootstrap& b = rt.bootstrap();
    if (!isClassObject(b, ctx, obj)) {
        bool declared = false;
        modifierOf(rt, ctx, obj, name, &declared);
        return declared;
    }
    // A class object: its class-instance variables, which each class on its
    // chain records on its metaclass.
    const proto::ProtoString* ivKey = instVarNamesKey(ctx);
    bool found = false;
    forClassAndAncestors(ctx, obj, [&](const proto::ProtoObject* k) {
        if (found || !isAttributeHolder(ctx, k)) return;
        const std::vector<std::string> names =
            ownWords(ctx, k->getOwnAttributeDirect(ctx, b.sym.metaclass), ivKey);
        found = std::find(names.begin(), names.end(), name) != names.end();
    });
    return found;
}

IvarModifier receiverModifierOf(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                const std::string& name, bool* declared) {
    if (isClassObject(rt.bootstrap(), ctx, obj)) {
        if (declared) *declared = declaresInstVar(rt, ctx, obj, name);
        return IvarModifier::Plain;
    }
    return modifierOf(rt, ctx, obj, name, declared);
}

std::vector<std::string> constantNamesOf(STRuntime& rt, proto::ProtoContext* ctx,
                                         const proto::ProtoObject* cls) {
    std::vector<std::string> out;
    if (!isChainedObject(ctx, cls)) return out;
    std::unordered_set<std::string> seen;
    collectConstants(rt.bootstrap(), ctx, cls, out, seen);
    return out;
}

} // namespace protoST
