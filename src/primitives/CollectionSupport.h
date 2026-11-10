#pragma once
// The few things a primitive file outside collection_prims.cpp needs to build
// and read protoST collections from C++ (msgpack_prims.cpp is the user). The
// representation (a mutable child of the class prototype holding `__data__`)
// stays private to collection_prims.cpp; everything goes through here, so there
// is one place that knows it.

#include "protoST/STRuntime.h"
#include "protoCore.h"

#include <utility>
#include <vector>

namespace protoST {

// True when `obj` has `target` in its prototype chain (the walk stops at the
// protoST Object root, as the collection primitives do).
bool inheritsFromProto(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                       const proto::ProtoObject* target, const proto::ProtoObject* objectProto);

// A new Array holding `data`.
const proto::ProtoObject* makeArrayInstance(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoList* data);

// A new, empty Dictionary; dictionaryPut adds (or replaces) an entry.
const proto::ProtoObject* makeEmptyDictionary(STRuntime& rt, proto::ProtoContext* ctx);
void dictionaryPut(proto::ProtoContext* ctx, const proto::ProtoObject* dict,
                   const proto::ProtoObject* key, const proto::ProtoObject* value);

// True for an instance of Array or OrderedCollection (or a subclass of either).
bool isArrayOrOrdered(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* o);
// The elements of such an instance, as the immutable list it holds.
const proto::ProtoList* arrayOrOrderedElements(proto::ProtoContext* ctx, const proto::ProtoObject* o);
// A list (or map snapshot) as a value the collector can be told to keep: the
// same pointer protoCore uses for it, typed as an object.
const proto::ProtoObject* asRootable(const proto::ProtoList* list);

// True for a Dictionary (or subclass) instance; its entries in protoCore's
// order, which is unspecified.
bool isDictionary(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* o);
// Answers the snapshot map the entries point into: the caller keeps it
// reachable (pinned or rooted) for as long as it uses `out`.
const proto::ProtoObject* dictionaryEntries(proto::ProtoContext* ctx, const proto::ProtoObject* dict,
                       std::vector<std::pair<const proto::ProtoObject*, const proto::ProtoObject*>>& out);

// Associations.
const proto::ProtoObject* makeAssociationValue(STRuntime& rt, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* key,
                                               const proto::ProtoObject* value);
bool isAssociation(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* o);
const proto::ProtoObject* associationKey(proto::ProtoContext* ctx, const proto::ProtoObject* assoc);
const proto::ProtoObject* associationValue(proto::ProtoContext* ctx, const proto::ProtoObject* assoc);

} // namespace protoST
