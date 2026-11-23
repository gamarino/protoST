#pragma once
// The state messages of explicit-state spec §6.4 and §6.5:
// compareAndSet:from:to:, update:with: and the deprecated alias
// setInstVar:from:to: (state_prims.cpp).

namespace protoST {

class STRuntime;

// Binds the state messages on Object. Called beside installAtomPrimitives.
void installStatePrimitives(STRuntime& rt);

// Whether an update:with: block is running on this thread (plan decision
// D-f). prim_Future_wait tests it first and signals M7 when it is (spec
// §10.3): a wait inside a block that may run several times is an error.
bool insideUpdateBlock();

} // namespace protoST
