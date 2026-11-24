#pragma once
// The state messages of explicit-state spec §6.4 and §6.5:
// compareAndSet:from:to:, update:with: and the deprecated alias
// setInstVar:from:to:, and the probe instVarIsSet: of §6.8
// (state_prims.cpp).

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;

// Binds the state messages on Object. Called beside installAtomPrimitives.
void installStatePrimitives(STRuntime& rt);

// Whether an update:with: block is running on this thread (plan decision
// D-f). prim_Future_wait tests it first and signals M7 when it is (spec
// §10.3): a wait inside a block that may run several times is an error.
bool insideUpdateBlock();

// Whether protoCore can write `r`'s attributes: a mutable object answers
// true; an immutable one, or a non-object such as a SmallInteger, false.
// Deterministic, with no visible write (Task 7 review 1, I2). The state
// primitives call it only after a failed CAS, to tell a lost race from an
// immutable receiver.
bool receiverIsWritable(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* r);

} // namespace protoST
