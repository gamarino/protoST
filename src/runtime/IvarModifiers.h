#pragma once
// Instance-variable modifiers (explicit-state spec §6): `constant`,
// `writeOnce` and `atomic`, declared by the constantNames:, writeOnceNames:
// and atomicNames: clauses of a class declaration. Plain variables are the
// ones instanceVariableNames: declares.
//
// The records live on the class object (plan decision D-a):
//   __instvar_names__        every declared name, in source order (as before);
//   __ivar_modifiers__       a String of `kind:name` words for the class's OWN
//                            non-plain variables, e.g.
//                            'constant:number constant:owner atomic:balance';
//   __has_constants__        true on a class that declares a constant; read
//                            with getAttribute, so subclasses see it;
//   __constant_constructor__ the generated constructor's Symbol (Task 6).
//
// Modifiers apply to instance variables only: class-instance variables and
// class variables take none (plan ruling G1).

#include <string>
#include <unordered_set>
#include <vector>

#include "protoCore.h"
#include "runtime/IvarModifierKinds.h"
#include "runtime/ListIndex.h"
#include "runtime/TransientPin.h"

namespace protoST {

class STRuntime;

// The names a class declares, recorded on the class object as one
// space-separated string under `key`: `__instvar_names__` (instance
// variables; on a metaclass, class-instance variables) and
// `__classvar_names__` (class variables). Adds them to `out`.
void addDeclaredNames(proto::ProtoContext* ctx, const proto::ProtoObject* o,
                      const proto::ProtoString* key, std::unordered_set<std::string>& out);

// Calls `f` on `cls` and then on each of its ancestors (superclasses and
// mixins: protoCore's flattened parent chain), nearest first.
template <typename F>
void forClassAndAncestors(proto::ProtoContext* ctx, const proto::ProtoObject* cls, F&& f) {
    f(cls);
    if (const proto::ProtoList* chain = cls->getParents(ctx)) {
        TransientPin pinChain(ctx, reinterpret_cast<const proto::ProtoObject*>(chain));
        const proto::proto_ulong n = chain->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i) {
            const proto::ProtoObject* p = chain->getAt(ctx, listIndex(i));
            if (p && p != PROTO_NONE) f(p);
        }
    }
}

// The entries of `cls`'s OWN `__ivar_modifiers__`, in source order (empty
// when the class records none). Throws NativeError on a malformed record.
std::vector<ModifiedIvar> ownModifiersOf(proto::ProtoContext* ctx, const proto::ProtoObject* cls);

// The instance side. `obj` is a class or an instance; its class chain is
// walked (the class, its superclasses and its mixins, as
// forClassAndAncestors does), so a class answers for its instances' variables.
// `declared` (when not null) is set to whether the name is a declared instance
// variable; an undeclared name answers Plain.
IvarModifier modifierOf(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* obj,
                        const std::string& name, bool* declared);

// Whether `obj` declares `name` as one of ITS variables: for an instance,
// its declared instance variables (own and inherited, mixins included); for a
// class object, its class-instance variables (pre-flight ruling C10).
bool declaresInstVar(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* obj,
                     const std::string& name);

// The modifier of the variable `name` of the receiver `obj` of a state message
// (compareAndSet:from:to:, update:with:, instVarNamed:(put:), instVarIsSet:).
// An instance answers as modifierOf. A class object's variables are its
// class-instance variables, which are always plain (ruling C10, G1).
IvarModifier receiverModifierOf(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* obj,
                                const std::string& name, bool* declared);

// The constants of `cls`'s instances, inherited first: the superclass's
// (recursively), then each mixin's in listed order, then the class's own, each
// class's in source order, every name once.
std::vector<std::string> constantNamesOf(STRuntime&, proto::ProtoContext*,
                                         const proto::ProtoObject* cls);

// The same, with `ownConstants` standing for the class's own constants (in
// source order) instead of its records: the list a declaration that is about
// to be recorded would give, so it can be checked before anything changes.
std::vector<std::string> constantNamesOf(STRuntime&, proto::ProtoContext*,
                                         const proto::ProtoObject* cls,
                                         const std::vector<std::string>& ownConstants);

} // namespace protoST
