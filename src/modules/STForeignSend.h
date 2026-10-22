#pragma once
#include "protoCore.h"

namespace protoST {

class STRuntime;

// The cross-runtime send entry: how a caller that is not protoST sends a
// Smalltalk message to a protoST object.
//
// A protoST method is not a proto::ProtoMethod: it is an object carrying
// `__bc_ptr__`, which only protoST's ExecutionEngine can run. Another runtime
// on protoCore can hold a protoST object (the values cross with no copy, Track
// Y) but cannot run its methods. This entry is the one protoCore method that
// can: STRuntime installs it on protoST's root Object prototype under
// `kForeignSendSelector`, so every protoST class and instance answers it, and a
// caller needs protoCore alone to use it:
//
//     const ProtoObject* send = obj->getAttribute(ctx, createSymbol(ctx, "__st_send__"));
//     const ProtoObject* args[] = {createSymbol(ctx, "at:put:"), key, value};
//     send->asMethod(ctx)(ctx, obj, nullptr, &ProtoArguments(args, 3), nullptr);
//
// The contract:
//   * `self` is the receiver; positional argument 0 is the selector, the exact
//     Smalltalk selector as a String or Symbol (`increment`, `at:put:`, `+`);
//     the remaining positional arguments are the message's arguments. Mapping a
//     caller's own surface onto a selector is the caller's concern.
//   * Named arguments are refused: a Smalltalk message has none.
//   * The message is sent as a SEND does: class-side methods answer only a
//     class, an unknown selector runs `doesNotUnderstand:` or signals
//     MessageNotUnderstood, a primitive's failure is signalled as an Error.
//   * Values cross unchanged, in both directions: the arguments are the
//     caller's cells and the result is protoST's.
//   * For a caller whose ProtoSpace protoST does not own, the message runs on
//     protoST's own current context, which only the thread that constructed the
//     runtime may use (D26), so a call from any other thread is refused. The
//     handlers of protoST code further up the stack are disabled while it runs:
//     a foreign runtime's frames lie between them and this send, and an unwind
//     to one of them would have to cross those frames.
//   * Errors leave as protoCore 3.0.0 exceptions (docs/EXCEPTIONS.md, E7): an
//     Error no protoST handler took leaves as the UnhandledSTException that
//     carries it, a foreign exception (a module's, another language's) as the
//     very exception that arrived, unchanged (E3). A protoST control-flow
//     unwind that would leave the entry (a non-local return, `retry`,
//     `resume:`, a Future yield) cannot cross a runtime boundary and is
//     reported as a std::runtime_error.
inline constexpr const char* kForeignSendSelector = "__st_send__";

const proto::ProtoObject* foreignSendEntry(proto::ProtoContext* callerCtx,
                                           const proto::ProtoObject* self,
                                           const proto::ParentLink* parentLink,
                                           const proto::ProtoArguments* positional,
                                           const proto::ProtoKeywordArguments* keywords);

// Installs the entry on `rt`'s root Object prototype. Called once, from the
// STRuntime constructor.
void installForeignSendEntry(STRuntime& rt);

} // namespace protoST
