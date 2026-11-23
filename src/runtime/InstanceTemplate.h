#pragma once

// Class instance templates.
//
// An instance of a protoST class is a mutable protoCore object whose parents
// are the class and the class's ancestors, and whose instance variables are
// own attributes named "_iv_<name>". Until protoST 0.13 `basicNew` made each
// instance with `cls->newChild`: a fresh parent link per instance, and an
// empty attribute shape that every first assignment of a variable extended,
// so no two instances of a class shared a ProtoShape and protoCore's
// per-thread shape cache could not serve one instance's lookups from
// another's.
//
// In protoCore's object-shape model (unreleased, after 3.6.0) an object's
// attributes are a shared ProtoShape (the names) plus value slots, and
// ProtoObject::newClone
// makes a sibling that shares the receiver's parent link and shape. So each
// class now keeps one immutable TEMPLATE: a child of the class whose shape
// names every instance variable the class declares or inherits (its
// `__instvar_names__` and those of every class on its chain), with every slot
// unset. `basicNew` answers `template->newClone(ctx, true)`, a mutable
// instance in one cell (two with its birth snapshot) that shares the
// template's parent link and shape. Assigning a declared variable then
// replaces a slot value; the shape is shared by every instance of the class.
//
// An unset slot is invisible: reads fall through to the parent chain exactly
// as for an absent attribute (nil, or a class variable of the same name), and
// hasOwnAttribute, instVarNamed:, reflection and printing see only the
// variables an instance has assigned, as before.
//
// The template is cached on the class under `__instance_template__`, with the
// layout epoch it was built in under `__instance_template_epoch__`. Any
// declaration of instance variables (declareInstVarNames) advances the epoch,
// so a re-declared class, and every subclass that inherits the change, builds
// a new template at its next basicNew. A template is also rebuilt when its
// parent is not the class it is read from: a class rebuilt by addBehavior:
// copies its predecessor's own attributes, template included. A stale
// template could never answer a wrong value, only cost speed: a variable it
// lacks is added to the instance's shape on first assignment, and a name it
// has in excess stays an invisible unset slot.

namespace proto {
    class ProtoContext;
    class ProtoObject;
}

namespace protoST {

class STRuntime;

// The template `basicNew` clones for instances of `cls`, building and caching
// it when the class has none or a stale one. nullptr when `cls` is not an
// object (then there is nothing to clone).
const proto::ProtoObject* instanceTemplate(STRuntime& rt, proto::ProtoContext* ctx,
                                           const proto::ProtoObject* cls);

// A new mutable instance of `cls`: a clone of its template, or, when it has
// none, a plain mutable child.
const proto::ProtoObject* newInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* cls);

// Invalidates every template: called when any class declares instance
// variables.
void noteInstanceLayoutChanged();

// The current instance-layout epoch: a cache derived from class declarations
// (templates, the constant lists of ConstantConstructor.h) records the epoch it
// was built in and is rebuilt once it differs.
long long instanceLayoutEpoch();

} // namespace protoST
