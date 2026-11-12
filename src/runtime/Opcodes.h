#pragma once
#include <cstddef>
#include <cstdint>

namespace protoST {

enum class Op : uint8_t {
    // 0 is reserved (it was NOP, which the compiler never emitted).
    // Stack and constants
    PUSH_CONST      = 1,    // arg = constant pool index
    PUSH_LOCAL      = 2,    // arg = slot index
    STORE_LOCAL     = 3,    // arg = slot index
    DUP             = 4,
    POP             = 5,
    // Values
    PUSH_SELF       = 6,
    // 7 is reserved (it was PUSH_SUPER, which pushed self and made the next
    // send in the frame a super send; see SEND_SUPER).
    PUSH_NIL        = 8,
    PUSH_TRUE       = 9,
    PUSH_FALSE      = 10,
    // Sends
    SEND_UNARY      = 11,   // arg = selector const index
    SEND_BINARY     = 12,   // arg = selector const index; 1 arg on stack
    SEND_KEYWORD    = 13,   // arg = selector const index; argc encoded in selector
    // A send whose receiver is `super`: the compiler pushes self, then the
    // arguments, and the lookup starts above the class that defines the
    // running method. Argument count: one per colon of the selector, or one
    // for a binary selector (BytecodeModule::sendArgCount).
    SEND_SUPER      = 14,   // arg = selector const index
    // Control flow
    JUMP            = 15,   // arg = forward offset in instructions (i.e. multiple of 2 bytes)
    JUMP_IF_TRUE    = 16,
    JUMP_IF_FALSE   = 17,
    // Return
    RETURN          = 18,
    RETURN_TOP      = 19,   // pops top, returns
    // Blocks
    PUSH_BLOCK      = 20,   // arg = bytecode-module-table index
    // 21 is reserved (it was DUP_RECEIVER, never emitted: cascades use DUP).
    // Closures (F3)
    PUSH_CAPTURED   = 22,   // arg = constant pool index (symbol name)
    STORE_CAPTURED  = 23,   // arg = constant pool index (symbol name)
    // User classes (F4)
    PUSH_GLOBAL     = 24,   // arg = constant pool symbol index (name of global)
    STORE_GLOBAL    = 25,   // arg = constant pool symbol index
    PUSH_INSTVAR    = 26,   // arg = constant pool symbol index (name of inst var, read on the frame's self)
    STORE_INSTVAR   = 27,   // arg = constant pool symbol index
    // Closure capture in methods (CLO): allocate a fresh per-method captured
    // dict and install it in frame slot 0 (no operand; pass 0).
    MAKE_CAPTURED   = 28,
    // Collection literals (COL-a): pop `arg` values off the operand stack
    // (oldest pushed first → element 0) and push a fresh Array instance
    // wrapping them. `arg` is wide-operand capable (EXTEND), so a literal may
    // contain more than 255 elements. Backs both `#(...)` and `{...}`.
    MAKE_ARRAY      = 29,
    // 2026-05-24: backward jump used by the compiler's doYielding: desugar.
    // arg = backward offset in instructions (i.e. multiple of 2 bytes).
    // The dispatch loop computes f.pc = f.pc - arg * kInstrSize, taking pc
    // BACK to the loop test. Complement of JUMP (forward).
    JUMP_BACK       = 30,
    // 2026-05-24: SmallInt binary fast-path opcodes. Each tag-checks both
    // operands as SmallInt and, on a hit, replaces them with the result
    // inline (no SEND dispatch, no primitive call, no method lookup). On a
    // miss, falls through to the SEND_BINARY handler with the same `arg`
    // (selector const index) so user-defined `+` / `<=` / etc. on
    // non-Integer receivers keep working unchanged. The compiler emits
    // these opcodes for every binary send whose selector is one of
    // `+ - <= < >= > =`; the rest of receiver-type discrimination is the
    // runtime tag check, which is one branch.
    BIN_INT_ADD     = 31,   // +
    BIN_INT_SUB     = 32,   // -
    BIN_INT_LE      = 33,   // <=
    BIN_INT_LT      = 34,   // <
    BIN_INT_GE      = 35,   // >=
    BIN_INT_GT      = 36,   // >
    BIN_INT_EQ      = 37,   // =
    // The type guard of an inlined whileTrue: / whileFalse: condition,
    // before its JUMP_IF_FALSE / JUMP_IF_TRUE (which treat anything but
    // false / true as the other value). A Boolean on top of the stack is
    // left in place. Any other value is replaced by the truth that
    // loopConditionIsTrue answers: it signals a resumable
    // NonBooleanReceiver, and the resumed value counts as true unless it
    // is false (Pharo's rule), the rule of the loop primitives too.
    // Named ASSERT_BOOL_OR_DNU until 0.10.0, when it sent
    // doesNotUnderstand: of the loop selector to the condition's value.
    ASSERT_LOOP_BOOL = 38, // arg = selector const index (for the message)
    // 2026-06-13: protoCore-style call-form send (positional + named args).
    // The single operand is a const-pool index naming a *mangled* selector
    // of the form `<name>#<nPos>[#<sortedKey1>#<sortedKey2>...]`. The
    // dispatcher parses the mangle to recover (name, nPos, sortedKeys),
    // pops `1 + nPos + nNamed` values (receiver + positionals in source
    // order + named values in alphabetical-key order), looks up the bare
    // `<name>` attribute on the receiver, and dispatches with the protoCore
    // method convention. See docs/archive/design-specs/2026-06-13-protocore-call-syntax.md.
    SEND_CALL          = 39, // arg = const-pool index of mangled call selector
    // Binds a name in the CURRENT activation's captured dict (own attribute),
    // popping the value. Used by the capture prolog for a scope's declared
    // captured names: unlike STORE_CAPTURED, which writes to the dict that
    // already owns the name up the parent chain, this creates the binding
    // here, so a block parameter or temporary that reuses an enclosing name
    // is a distinct variable.
    DEFINE_CAPTURED    = 40, // arg = constant pool index (symbol name)
    // Writes a class variable: pops the value and stores it under the mangled
    // "_iv_<name>" key on the object that declares the variable, found by
    // walking up the prototype chain from `self` (an instance or a class).
    // Class variables are shared by the declaring class, its subclasses and
    // all their instances, so a write from any of them updates that storage.
    STORE_CLASSVAR     = 41, // arg = constant pool symbol index (variable name)
    // Reads a class-side instance variable: the mangled "_iv_<name>" slot of
    // `self` (a class object) itself, nil when unset — never a superclass's.
    PUSH_OWN_INSTVAR   = 42, // arg = constant pool symbol index (variable name)
    // Instance-variable write groups (Compiler::tryEmitInstVarGroup). A run
    // of statements `v1 := e1. v2 := e2. ... vn := en.` assigning instance
    // variables of `self`, where e2..en cannot run code or observe `self`,
    // compiles to
    //     <e1> IVAR_GROUP <e2> IVAR_GROUP ... <en> IVAR_GROUP_END
    // and is published as ONE new version of `self`
    // (ProtoObject::setAttributes). Each value stays on the operand stack
    // until IVAR_GROUP_END, which leaves only the last one (the value of the
    // run as a statement). `arg` indexes the module's group-entry table
    // (BytecodeModule::instVarGroupEntry): the variable's name constant, its
    // position in the run and the run's length. When the engine declines the
    // group (a debugger is attached) or must publish early (a SmallInteger
    // operator in a later value misses its fast path and sends a message),
    // every write takes STORE_INSTVAR's path at its own position instead.
    IVAR_GROUP         = 43, // arg = group-entry index; leaves the value on the stack
    IVAR_GROUP_END     = 44, // arg = group-entry index of the run's last write
    // Peeks the top of the stack and jumps forward when it is neither true
    // nor false; a Boolean stays and execution falls through. It opens every
    // inlined value conditional (ifTrue:, ifFalse:, ifTrue:ifFalse:,
    // ifFalse:ifTrue:, and:, or:): the target is the out-of-line real send
    // of the expression (DEOPT_ENTER below) followed by a JUMP to its end.
    JUMP_IF_NOT_BOOL   = 45, // arg = forward offset in instructions
    // De-optimisation of an inlined value conditional whose receiver is not
    // a Boolean (0.10.0). The out-of-line target of JUMP_IF_NOT_BOOL sends
    // the selector for real, with closures compiled from the block literals:
    //     DEOPT_ENTER k  PUSH_LOCAL box  PUSH_DEOPT_BLOCK b1 [PUSH_LOCAL box
    //     PUSH_DEOPT_BLOCK b2]  SEND_KEYWORD #sel  DEOPT_EXIT k  JUMP end
    // DEOPT_ENTER moves the temporaries the closures use out of their frame
    // slots into the dictionary of the scope that declares them, leaving a
    // forward in each slot, so the frame and the closures share them from
    // then on, as with real closures; it builds the box, a captured dict
    // whose parent is the innermost of those scope dictionaries and which
    // holds copies of the arguments the closures read, and stores it in the
    // entry's frame slot. Nothing changes on the operand stack.
    DEOPT_ENTER        = 46, // arg = deopt-entry index
    // Pops a box and pushes a closure of sub-module `arg` that resolves its
    // free variables in that box (PUSH_BLOCK uses the frame's captured dict).
    PUSH_DEOPT_BLOCK   = 47, // arg = bytecode-module-table index
    // Clears the box's frame slot. The send's answer stays on the stack.
    DEOPT_EXIT         = 48, // arg = deopt-entry index
    // Sets a local slot to nil without following a forward: the entry of
    // an inlined block's scope gives each of its temporaries a fresh
    // variable, while a closure that shares the previous one keeps it.
    RESET_LOCAL        = 49, // arg = slot index
    // The inlined nil tests (ifNil:, ifNotNil:, ifNil:ifNotNil:,
    // ifNotNil:ifNil: with literal blocks; 0.10.0). Both peek: the value
    // stays on the stack on both paths.
    JUMP_IF_NIL        = 50, // arg = forward offset; jumps when the top is nil
    JUMP_IF_NOT_NIL    = 51, // arg = forward offset; jumps when it is not nil
    // `a == b`: pops two values, pushes whether they are the same object.
    // Never a send: `==` is not looked up, as in Pharo (0.10.0).
    IDENTICAL          = 52,
    // More SmallInteger fast paths (0.10.0), like BIN_INT_ADD: both operands
    // SmallIntegers and the result a SmallInteger, else the instruction is
    // the send of its selector constant (`arg`). A zero divisor is a miss
    // too, so the send's primitive signals ZeroDivide.
    BIN_INT_MUL        = 53, // *
    BIN_INT_MOD        = 54, // \\  (floored)
    BIN_INT_QUO        = 55, // //  (floored)
    // 56 is reserved: it was BIN_INT_REM, dropped before 0.10.0 shipped
    // because rem: is not one of Pharo's special selectors (review R3-1).
    BIN_INT_AND        = 57, // bitAnd: (a keyword send on a miss)
    BIN_INT_OR         = 58, // bitOr:  (a keyword send on a miss)
    // PUSH_LOCAL and STORE_LOCAL of a slot whose variable DEOPT_ENTER may
    // move into a scope dictionary: when the frame holds forwards, they
    // follow the slot's forward to the variable. The compiler emits
    // PUSH_LOCAL / STORE_LOCAL and turns those of such slots into these
    // once the module is complete (BytecodeModule::routeSharedLocals), so
    // the access of every other slot tests nothing.
    PUSH_SHARED_LOCAL  = 59, // arg = slot index
    STORE_SHARED_LOCAL = 60, // arg = slot index
    // Prefix carrying the next-higher byte of the following instruction's
    // operand, for operands above 255 (constant indices, slots, jumps).
    EXTEND          = 254,
    // 255 is reserved (it was HALT, never emitted: `halt` is a primitive).
};

inline constexpr size_t kInstrSize = 2;

} // namespace protoST
