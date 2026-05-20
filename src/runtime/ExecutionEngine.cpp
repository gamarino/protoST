// ExecutionEngine — non-recursive Smalltalk bytecode dispatcher.
//
// COOPERATIVE YIELD LIMITATION (F6 v3 v1):
//   Block invocation via direct `value` / `value:` / `value:value:` / ...
//   SEND is non-recursive (F6 v3 A2: handled inline in this engine by
//   pushing a Frame) and therefore yieldable from inside the block body.
//
//   Block invocation via primitives that take the block as an argument
//   and evaluate it internally — `ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:`,
//   `whileTrue:`, `thenDo:`, `catch:` — currently goes through
//   block_prims.cpp::invokeBlock, which spins up a fresh recursive
//   ExecutionEngine on the C++ stack. A `Future>>wait` inside such a
//   primitive-evaluated block will block its worker thread the F6 v2 way
//   and is NOT cooperatively yieldable.
//
//   The common digital-twin patterns (sequential statements with `wait`
//   and a direct `[ ... ] value`) are fully cooperative; loops and
//   conditionals containing `wait` are not. Lifting those primitives into
//   the engine is future work.
#include "ExecutionEngine.h"
#include "BytecodeModule.h"
#include "Bootstrap.h"
#include "FutureYield.h"
#include "SchedDiag.h"
#include "Opcodes.h"
#include "ActorLock.h"
#include "GcSafeMutex.h"
#include "debugger/DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "protoCore.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

namespace {

// F6 v3 E3: per-frame slot geometry.
//
// The frame stack is backed by the engine context's automaticLocals (a flat,
// GC-traced array). Each frame needs a FIXED region size decided at push
// time, since automaticLocals is pre-sized once and never grown afterwards.
//
// localCount: scan the module's bytecode for the highest local-slot index
//   referenced by PUSH_LOCAL / STORE_LOCAL, take +1, and floor it at the
//   module's declared argCount and a small minimum. The legacy engine grew
//   `locals` lazily via ensureLocal; this pre-computes the same ceiling.
// maxStack: a fixed conservative bound. Smalltalk method/block bodies in
//   this language are small; 48 operand slots is far past any real depth
//   (the legacy engine merely `reserve`d 64 and grew on demand).
constexpr unsigned int kMinLocals    = 4;
constexpr unsigned int kFrameMaxStk  = 48;

// F6 v3 E3: thread-local high-water mark into the engine context's
// automaticLocals. A ProtoContext is per-thread, but several ExecutionEngines
// can nest on the SAME thread's C++ stack on the SAME context — e.g. a
// primitive (`ifTrue:`, `thenDo:`, `whileTrue:`, `value`) called from inside
// an opcode handler spins up a fresh ExecutionEngine via invokeBlock(). Each
// engine therefore cannot assume slot 0 is free: it must pack its frame
// regions ABOVE whatever the outer engines already occupy.
//
// g_slotCursor is the next free slot on the current thread. pushFrame
// allocates [cursor, cursor+regionSize) and advances it; popping a frame
// rewinds it. Because engines nest strictly (LIFO) on the C++ stack, the
// cursor is a correct shared allocator: a nested engine sees the cursor
// already advanced past the outer engine's live frames, and on return the
// outer engine's cursor is exactly where it left it.
//
// Cooperative yield clears frames_ and rewinds the cursor by the engine's
// own consumed span (cursor - slotBase_); a resumed engine re-allocates from
// the cursor as restoreFrames rebuilds the stack.
thread_local unsigned int g_slotCursor = 0;

unsigned int computeLocalCount(const BytecodeModule& m, unsigned int argc) {
    const auto& bytes = m.bytes();
    unsigned int maxSlot = 0;
    bool sawSlot = false;
    for (std::size_t pc = 0; pc + 1 < bytes.size(); pc += kInstrSize) {
        const Op op = static_cast<Op>(bytes[pc]);
        if (op == Op::PUSH_LOCAL || op == Op::STORE_LOCAL) {
            unsigned int slot = bytes[pc + 1];
            if (!sawSlot || slot > maxSlot) { maxSlot = slot; sawSlot = true; }
        }
    }
    unsigned int needed = sawSlot ? (maxSlot + 1) : 0;
    needed = std::max(needed, argc);
    needed = std::max(needed, kMinLocals);
    return needed;
}

} // namespace

// ---------------------------------------------------------------------------
// F6 v3 E3: slot accessors. Every ProtoObject* a frame touches is read/written
// through the engine context's automaticLocals so the GC sees it.
// ---------------------------------------------------------------------------

const proto::ProtoObject*
ExecutionEngine::getLocal(const Frame& f, unsigned int i) const {
    return ctx_->getAutomaticLocal(localsBase(f) + i);
}
void
ExecutionEngine::setLocal(const Frame& f, unsigned int i,
                          const proto::ProtoObject* v) {
    ctx_->setAutomaticLocal(localsBase(f) + i, v);
}
const proto::ProtoObject*
ExecutionEngine::getSelf(const Frame& f) const {
    return ctx_->getAutomaticLocal(f.baseSlot + 1);
}
void
ExecutionEngine::setSelf(const Frame& f, const proto::ProtoObject* v) {
    ctx_->setAutomaticLocal(f.baseSlot + 1, v);
}
const proto::ProtoObject*
ExecutionEngine::getCaptured(const Frame& f) const {
    return ctx_->getAutomaticLocal(f.baseSlot + 0);
}
void
ExecutionEngine::setCaptured(const Frame& f, const proto::ProtoObject* v) {
    ctx_->setAutomaticLocal(f.baseSlot + 0, v);
}
void
ExecutionEngine::push(Frame& f, const proto::ProtoObject* v) {
    if (f.sp >= f.maxStack)
        throw std::runtime_error("engine operand stack overflow");
    ctx_->setAutomaticLocal(opStackBase(f) + f.sp, v);
    ++f.sp;
}
const proto::ProtoObject*
ExecutionEngine::pop(Frame& f) {
    if (f.sp == 0)
        throw std::runtime_error("engine operand stack underflow");
    --f.sp;
    return ctx_->getAutomaticLocal(opStackBase(f) + f.sp);
}
const proto::ProtoObject*
ExecutionEngine::peek(const Frame& f) const {
    if (f.sp == 0)
        throw std::runtime_error("engine operand stack peek on empty");
    return ctx_->getAutomaticLocal(opStackBase(f) + f.sp - 1);
}
const proto::ProtoObject*
ExecutionEngine::opAt(const Frame& f, unsigned int depth) const {
    // depth 0 == top of stack.
    return ctx_->getAutomaticLocal(opStackBase(f) + f.sp - 1 - depth);
}

void
ExecutionEngine::pushFrame(const BytecodeModule* m,
                           const proto::ProtoObject* self,
                           const proto::ProtoObject* captured,
                           const proto::ProtoObject* const* args,
                           unsigned int argc) {
    Frame fr;
    fr.m          = m;
    fr.pc         = 0;
    fr.localCount = computeLocalCount(*m, argc);
    fr.maxStack   = kFrameMaxStk;
    fr.sp         = 0;
    // Allocate this frame's region from the shared thread-local cursor so it
    // never overlaps an outer (nested-engine) frame's region.
    fr.baseSlot = g_slotCursor;
    const unsigned int regionSize = frameRegionSize(fr);
    const unsigned int regionEnd  = fr.baseSlot + regionSize;
    if (regionEnd > kSlotCapacity)
        throw std::runtime_error("engine slot region exhausted");

    // Initialise the whole region to PROTO_NONE (header + locals + opStack).
    for (unsigned int s = fr.baseSlot; s < regionEnd; ++s)
        ctx_->setAutomaticLocal(s, PROTO_NONE);

    // Header slots.
    ctx_->setAutomaticLocal(fr.baseSlot + 0, captured ? captured : PROTO_NONE);
    ctx_->setAutomaticLocal(fr.baseSlot + 1, self ? self : PROTO_NONE);

    // Bind incoming args into locals 0..argc-1.
    for (unsigned int i = 0; i < argc; ++i)
        ctx_->setAutomaticLocal(localsBase(fr) + i,
                                args[i] ? args[i] : PROTO_NONE);

    g_slotCursor += regionSize;
    frames_.push_back(fr);
}

// F6 v3 E3: pop the top frame and rewind the shared slot cursor by exactly
// its region size. Frames are pushed/popped strictly LIFO, so the cursor
// always rewinds to the popped frame's baseSlot.
void
ExecutionEngine::popFrame() {
    if (frames_.empty()) return;
    const Frame& top = frames_.back();
    g_slotCursor = top.baseSlot;   // == g_slotCursor - frameRegionSize(top)
    frames_.pop_back();
}

const proto::ProtoObject*
ExecutionEngine::run(proto::ProtoContext* ctx,
                     const BytecodeModule& m,
                     const proto::ProtoObject* self) {
    return runWithArgs(ctx, m, self, nullptr, 0);
}

const proto::ProtoObject*
ExecutionEngine::runWithArgs(proto::ProtoContext* ctx,
                             const BytecodeModule& m,
                             const proto::ProtoObject* selfObj,
                             const proto::ProtoObject* const* args,
                             int argc,
                             const proto::ProtoObject* capturedDict) {
    // F6 v3 A: build the initial Frame and enter the single dispatch loop.
    // F6 v3 E3: the frame stack is backed by the engine context's
    // automaticLocals — a flat, GC-traced slot array. We pre-size that array
    // ONCE here, before any opcode runs and before any allocation that could
    // trigger a GC cycle. resizeAutomaticLocals is grow-only and idempotent,
    // so a worker context reused across many drainOne ticks pays the
    // reallocation cost at most once. Crucially, this thread is actively
    // running (not parked at a safepoint) during the resize, so a concurrent
    // stop-the-world GC cannot proceed mid-realloc — it needs every thread
    // parked first. No new ProtoContext is ever created.
    ctx_ = ctx;
    ctx_->resizeAutomaticLocals(kSlotCapacity);

    frames_.clear();
    frames_.reserve(64);

    // Capture where this engine's frame regions begin — the current
    // thread-local cursor, already advanced past any outer (nested) engine.
    slotBase_ = g_slotCursor;

    pushFrame(&m, selfObj, capturedDict, args,
              static_cast<unsigned int>(argc < 0 ? 0 : argc));

    return runLoop(ctx);
}

// F6 v3 E3: build a DebugFrame snapshot from a frame's slot region. The
// operand stack is the live [0, sp) portion; locals are all localCount slots.
DebugFrame
ExecutionEngine::makeDebugFrame(const Frame& f) const {
    DebugFrame d;
    d.module = f.m;
    d.pc     = f.pc;
    d.stack.reserve(f.sp);
    for (unsigned int i = 0; i < f.sp; ++i)
        d.stack.push_back(ctx_->getAutomaticLocal(opStackBase(f) + i));
    d.locals.reserve(f.localCount);
    for (unsigned int i = 0; i < f.localCount; ++i)
        d.locals.push_back(getLocal(f, i));
    return d;
}

const proto::ProtoObject*
ExecutionEngine::runLoop(proto::ProtoContext* ctx) {
    // F6 v3 E3: locals are no longer grown lazily — every frame's region is
    // pre-sized at pushFrame time (computeLocalCount scans the module for the
    // highest slot referenced). A slot index past localCount is a compiler /
    // sizing bug; reject it rather than silently aliasing another frame's
    // region.
    auto checkLocal = [](const Frame& f, uint8_t slot) {
        if (slot >= f.localCount)
            throw std::runtime_error("engine local slot out of range");
    };

    // F6 v3 E3: whatever happens (normal return, cooperative yield rethrow,
    // or a std::exception propagating out to drainOne), the shared slot
    // cursor MUST rewind to where this engine started — otherwise a later
    // engine on the same thread would leak slots and eventually hit the
    // "engine slot region exhausted" cap. On a clean RETURN popFrame() has
    // already rewound it to slotBase_; this guard makes the exceptional
    // exits equally safe and is a harmless no-op on the clean path.
    struct SlotCursorGuard {
        unsigned int restoreTo;
        ~SlotCursorGuard() { g_slotCursor = restoreTo; }
    } cursorGuard{slotBase_};

    // Outer loop: lets us resume after a DebuggerHalt is caught and the
    // user steps/conts out of the session. Without this wrapper the catch
    // handler would return early and the remainder of the module would be
    // skipped.
    while (true) {
    try {
    while (!frames_.empty()) {
        Frame& f = frames_.back();
        const auto& bytes = f.m->bytes();
        if (f.pc + 1 >= bytes.size()) {
            // Off the end without an explicit RETURN — treat as RETURN_TOP
            // with PROTO_NONE on an empty stack, matching the legacy
            // engine's fallthrough behaviour.
            const proto::ProtoObject* r =
                f.sp == 0 ? PROTO_NONE : peek(f);
            popFrame();
            if (frames_.empty()) return r;
            push(frames_.back(), r);
            continue;
        }

        // F2 single-step support: if the debugger is attached and in a
        // non-Free mode, enter the session BEFORE each instruction. The
        // session may flip mode back to Free (e.g. user typed 'c'), in
        // which case subsequent instructions run at full speed.
        // Calling enterSession directly (rather than throwing Halt) avoids
        // the cost of unwinding the C++ stack on every step.
        auto dbgMode = rt_.debugger().mode();
        if (rt_.debugger().attached() && dbgMode != DebuggerRuntime::Mode::Free) {
            rt_.debugger().enterSession(rt_, makeDebugFrame(f), "step");
            // Session may have updated mode (e.g. user typed 'c'); fall
            // through to dispatch the next instruction.
        }

        // F2 location breakpoint: halt BEFORE executing the instruction at pc.
        if (rt_.debugger().attached() && rt_.debugger().breakpoints().isSet(f.m, f.pc)) {
            rt_.debugger().enterSession(rt_, makeDebugFrame(f),
                                        "breakpoint");
        }

        const Op op = static_cast<Op>(bytes[f.pc]);
        const uint8_t arg = bytes[f.pc + 1];
        f.pc += kInstrSize;

        switch (op) {
            case Op::NOP:
                break;
            case Op::PUSH_NIL:
                push(f, PROTO_NONE);
                break;
            case Op::PUSH_TRUE:
                push(f, PROTO_TRUE);
                break;
            case Op::PUSH_FALSE:
                push(f, PROTO_FALSE);
                break;
            case Op::PUSH_CONST:
                push(f, rt_.materialize(*f.m, arg));
                break;
            case Op::DUP:
                push(f, peek(f));
                break;
            case Op::POP:
                pop(f);
                break;
            case Op::PUSH_LOCAL:
                checkLocal(f, arg);
                push(f, getLocal(f, arg));
                break;
            case Op::STORE_LOCAL: {
                checkLocal(f, arg);
                if (f.sp == 0)
                    throw std::runtime_error("STORE_LOCAL with empty stack");
                setLocal(f, arg, pop(f));
                break;
            }
            case Op::RETURN:
            case Op::RETURN_TOP: {
                // F4-U4: Op::RETURN is emitted by an explicit `^expr` in a
                // method or block body; Op::RETURN_TOP is emitted by the
                // compiler as the implicit trailer for a top-level module
                // and for blocks. Both pop the current frame's top-of-stack
                // and hand it back to the caller frame (or return it to the
                // C++ caller of run/runWithArgs when this was the last
                // frame).
                //
                // F6 v3 A: with the explicit Frame stack, "caller frame" is
                // simply frames_[size-2] after the pop. True non-local-return
                // semantics — `^` from inside a nested block unwinds to its
                // enclosing method — remains out of scope (the current test
                // suite does not exercise it; the legacy recursive engine
                // also did not implement it).
                // TODO(F6 v3 follow-up): support non-local return by walking
                // frames_ back to the enclosing method frame on Op::RETURN.
                const proto::ProtoObject* r =
                    f.sp == 0 ? PROTO_NONE : peek(f);
                popFrame();
                if (frames_.empty()) return r;
                push(frames_.back(), r);
                // Reference `f` is now dangling — continue the loop so the
                // next iteration re-acquires frames_.back().
                continue;
            }
            case Op::PUSH_BLOCK: {
                // arg = block index inside f.m->blocks(). Create a fresh
                // BlockClosure (a mutable child of the Block prototype) that
                // carries an opaque pointer to the sub-module under
                // attribute "__bc_ptr__".
                auto* blkProto = rt_.bootstrap().blockProto;
                auto* block = blkProto->newChild(ctx, /*isMutable=*/true);
                static const proto::ProtoString* bcKey =
                    ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const proto::ProtoString* capKey =
                    ctx->fromUTF8String("__captured__")->asString(ctx);
                auto* bcPtrObj = ctx->fromLong(
                    reinterpret_cast<long long>(&f.m->block(arg)));
                block->setAttribute(ctx, bcKey, bcPtrObj);
                // F3-C5: stash the current frame's captured dict so the
                // block can resolve free variables back to the outer scope
                // at invocation time.
                const proto::ProtoObject* capD = getCaptured(f);
                block->setAttribute(
                    ctx, capKey,
                    (capD && capD != PROTO_NONE) ? capD : PROTO_NONE);
                push(f, block);
                break;
            }
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::SEND_KEYWORD: {
                // pop N args (0 for unary, 1 for binary, count from selector
                // for keyword)
                int argcOp = (op == Op::SEND_UNARY)  ? 0
                           : (op == Op::SEND_BINARY) ? 1
                           : /* keyword */ 0;
                const std::string& selStr = f.m->constSymbol(arg);
                if (op == Op::SEND_KEYWORD) for (char c : selStr) if (c == ':') ++argcOp;

                if (static_cast<int>(f.sp) < argcOp + 1)
                    throw std::runtime_error("SEND with insufficient stack");
                if (argcOp > 8) throw std::runtime_error("F2 limit: <=8 args per send");
                // F6 v3 E3: pop args/receiver off the operand stack. `pop`
                // only decrements sp — the values remain physically in the
                // frame's slot region, which the GC traces in full (the whole
                // automaticLocals array is scanned, not just [0, sp)). So the
                // popped objects stay rooted while we build the send below,
                // even though `sendArgs` is a plain C++ array.
                const proto::ProtoObject* sendArgs[8];
                for (int i = argcOp - 1; i >= 0; --i)
                    sendArgs[i] = pop(f);
                const proto::ProtoObject* recv = pop(f);

                auto* selSym = ctx->fromUTF8String(selStr.c_str())->asString(ctx);

                // F6-A4: actor fast-path. If the receiver is an actor (i.e.
                // carries a __wrapped__ attribute installed by Object>>asActor),
                // we bypass the synchronous dispatch entirely. The send is
                // converted into a Message envelope, enqueued on the actor's
                // mailbox, the actor is scheduled, and a fresh pending Future
                // is pushed onto the operand stack as the apparent result of
                // the send. The actual method execution happens later when
                // STRuntime::drainOne pulls a message from the mailbox.
                if (rt_.isActor(ctx, recv)) {
                    static const proto::ProtoString* mbKey =
                        ctx->fromUTF8String("__mailbox__")->asString(ctx);
                    static const proto::ProtoString* msgSelKey =
                        ctx->fromUTF8String("__selector__")->asString(ctx);
                    static const proto::ProtoString* msgArgsKey =
                        ctx->fromUTF8String("__args__")->asString(ctx);
                    static const proto::ProtoString* msgFutKey =
                        ctx->fromUTF8String("__future__")->asString(ctx);

                    // Allocate a fresh pending Future.
                    auto* fut = const_cast<proto::ProtoObject*>(rt_.newFuture(ctx));

                    // Build the message envelope (a fresh mutable child of objectProto).
                    auto* msg = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
                        ->newChild(ctx, /*isMutable=*/true);

                    // Build args ProtoList by FIFO appendLast of each arg.
                    auto* argsList = ctx->newList();
                    for (int i = 0; i < argcOp; ++i) {
                        argsList = argsList->appendLast(ctx, sendArgs[i]);
                    }

                    msg->setAttribute(ctx, msgSelKey, reinterpret_cast<const proto::ProtoObject*>(selSym));
                    msg->setAttribute(ctx, msgArgsKey, argsList->asObject(ctx));
                    msg->setAttribute(ctx, msgFutKey, fut);

                    // F6 v2 T3: hold the per-actor lock across the mailbox
                    // read-modify-write. Without this, the worker thread's
                    // drainOne can pop a message between our getAttribute and
                    // setAttribute calls below; our setAttribute would then
                    // overwrite the popped state and either lose the popped
                    // message (if our append went on top of stale state) or
                    // double-process it.
                    //
                    // We pull the lock pointer outside the {} so we can hold
                    // the guard for the entire RMW and drop it before
                    // schedule() — schedule() takes schedMu internally and
                    // mixing the two acquisition orders elsewhere would risk
                    // deadlock (drainOne acquires schedMu first, then the
                    // actor lock).
                    std::mutex* actorLock = getActorLock(ctx, recv);
                    {
                        // F6 v3 E4: acquire the per-actor lock GC-safely. The
                        // drainOne holder keeps this lock across the entire
                        // user-method dispatch — i.e. across protoCore
                        // allocation — and may park at a GC safepoint while
                        // holding it. A plain std::mutex::lock() here would
                        // block this thread off-safepoint while it is still a
                        // counted mutator, stalling the STW quorum forever.
                        GcSafeLockGuard guard(ctx, actorLock);

                        // Enqueue (FIFO) into the actor's mailbox.
                        auto* mbObj = recv->getAttribute(ctx, mbKey);
                        auto* mailbox = mbObj ? mbObj->asList(ctx) : ctx->newList();
                        auto* newMailbox = mailbox->appendLast(ctx, msg);
                        const_cast<proto::ProtoObject*>(recv)
                            ->setAttribute(ctx, mbKey, newMailbox->asObject(ctx));
                    }

                    // Schedule the actor for processing and stash the Future
                    // as the apparent result of the send. schedule() acquires
                    // schedMu; doing it OUTSIDE the actor lock keeps the
                    // global acquisition order (schedMu first, actor lock
                    // second) consistent with drainOne.
                    rt_.schedule(ctx, recv);
                    push(f, fut);
                    break;
                }

                // F6 v3 A2: direct block invocation fast-path. If the
                // receiver carries `__bc_ptr__` as an OWN attribute (it is a
                // BlockClosure built by PUSH_BLOCK) AND the selector is a
                // `value`-family selector matching its formal-arg count, we
                // push a block Frame onto frames_ and let runLoop continue.
                // This is the equivalent of the user-method push-frame path
                // (F6 v3 A) but specialised for blocks: no self-binding into
                // slot 0, args go into slots 0..argcOp-1, captured dict
                // threaded from the closure's `__captured__` attribute.
                //
                // The legacy path (primitive `prim_Block_value` in
                // block_prims.cpp spinning a fresh recursive
                // ExecutionEngine) remains as a fallback for indirect block
                // invocation (e.g. via `perform:` once that exists, or any
                // dispatch path that does NOT come through SEND_UNARY /
                // SEND_KEYWORD here). It is also still used by
                // `ifTrue:`/`whileTrue:`/`thenDo:` primitives that take a
                // block as an argument and evaluate it internally — those
                // remain non-yieldable. See the limitation block at the top
                // of this file.
                {
                    static const proto::ProtoString* recvBcKey =
                        ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                    static const proto::ProtoString* recvCapKey =
                        ctx->fromUTF8String("__captured__")->asString(ctx);
                    // getAttribute walks the receiver first then the proto
                    // chain. `__bc_ptr__` is set only on BlockClosures (by
                    // PUSH_BLOCK), never on `blockProto` itself, so a hit
                    // here is unambiguous evidence that `recv` is a block.
                    auto* recvBcPtr = recv->getAttribute(ctx, recvBcKey);
                    if (recvBcPtr && recvBcPtr != PROTO_NONE) {
                        // Validate selector matches the value/value:/...
                        // pattern for argcOp formal parameters. argcOp is
                        // already computed from the selector colons above
                        // (or 0 for SEND_UNARY which uses bare `value`).
                        bool isValueFamily = false;
                        if (op == Op::SEND_UNARY) {
                            isValueFamily = (selStr == "value");
                        } else if (op == Op::SEND_KEYWORD) {
                            // selector must be exactly argcOp repetitions
                            // of "value:" — i.e. "value:", "value:value:",
                            // "value:value:value:", ...
                            isValueFamily = true;
                            std::string expected;
                            for (int i = 0; i < argcOp; ++i) expected += "value:";
                            if (selStr != expected) isValueFamily = false;
                        }
                        // SEND_BINARY never matches value-family.
                        if (isValueFamily) {
                            const protoST::BytecodeModule* sub =
                                reinterpret_cast<const protoST::BytecodeModule*>(
                                    recvBcPtr->asLong(ctx));
                            if (sub->argCount() != argcOp) {
                                throw std::runtime_error(
                                    "block arg count mismatch (expected " +
                                    std::to_string(sub->argCount()) +
                                    ", got " + std::to_string(argcOp) + ")");
                            }
                            auto* capDict = recv->getAttribute(ctx, recvCapKey);
                            if (!capDict || capDict == PROTO_NONE) capDict = nullptr;

                            // F6 v3 E3: blocks bind args into locals
                            // 0..argcOp-1 (no implicit self slot; cf.
                            // invokeBlock which passes self=PROTO_NONE).
                            // PUSH_INSTVAR inside a block reads local 0
                            // which would therefore be the first arg, not
                            // the enclosing method's self — same
                            // pre-existing limitation as the legacy
                            // invokeBlock path. pushFrame packs the
                            // callee's region into the engine context's
                            // GC-traced automaticLocals.
                            pushFrame(sub, /*self=*/PROTO_NONE, capDict,
                                      sendArgs,
                                      static_cast<unsigned int>(argcOp));
                            // `f` is now invalidated when frames_ grows
                            // its backing storage. Continue the loop so
                            // the next iteration re-acquires
                            // frames_.back() and dispatches the block
                            // from its pc=0.
                            continue;
                        }
                    }
                }

                // Lookup walks from the receiver itself first, then up the
                // prototype chain.  Starting at the receiver matters for
                // "class as receiver" patterns (F4): a class prototype
                // carries its instance methods as own-attributes, and
                // messages sent to the class itself must resolve to those
                // methods.  For ordinary instance receivers, getAttribute
                // walks past the (typically empty) own-attribute slot to the
                // prototype where the method is bound.
                //
                // Note: getAttribute on a tagged immediate (SmallInteger,
                // Boolean, ...) correctly delegates to its protoCore
                // prototype slot (smallIntegerPrototype etc.), which our
                // bootstrap has already re-pointed at the protoST
                // prototypes (see Bootstrap.cpp).
                auto* attr = recv->getAttribute(ctx, selSym);
                if (!attr || attr == PROTO_NONE) {
                    throw std::runtime_error("doesNotUnderstand: " + selStr);
                }
                // F4-U4: detect user method (Block-shaped wrapper carrying
                // __bc_ptr__). User methods are installed by Compiler-emitted
                // __installMethod:as: and are not tagged primitive markers.
                // We probe for __bc_ptr__ first; if absent, fall through to
                // the legacy primitive-marker (tagged SmallInteger with bit
                // 62 set) dispatch.
                static const proto::ProtoString* bcKey =
                    ctx->fromUTF8String("__bc_ptr__")->asString(ctx);
                static const proto::ProtoString* capKey =
                    ctx->fromUTF8String("__captured__")->asString(ctx);
                auto* bcPtrObj = attr->getAttribute(ctx, bcKey);
                if (bcPtrObj && bcPtrObj != PROTO_NONE) {
                    // User method: F6 v3 A previously recursed via
                    //   ExecutionEngine subEng(rt_);
                    //   subEng.runWithArgs(...)
                    // — i.e. a new C++ stack frame per Smalltalk call.
                    // Now we push a Frame onto our own frames_ instead and
                    // let runLoop pick it up. The callee's eventual
                    // RETURN/RETURN_TOP pops its frame and pushes its
                    // result onto OUR opStack (this very `f.opStack`,
                    // though by then `f` is a dangling reference).
                    const protoST::BytecodeModule* sub =
                        reinterpret_cast<const protoST::BytecodeModule*>(
                            bcPtrObj->asLong(ctx));
                    if (sub->argCount() != argcOp + 1) {
                        throw std::runtime_error(
                            "method " + selStr + " expects " +
                            std::to_string(sub->argCount() - 1) +
                            " args, got " + std::to_string(argcOp));
                    }
                    auto* capDict = attr->getAttribute(ctx, capKey);
                    if (capDict == PROTO_NONE) capDict = nullptr;

                    // F6 v3 E3: local 0 = recv (self), locals 1..argcOp =
                    // call args. pushFrame packs them into the callee's
                    // region inside the GC-traced automaticLocals.
                    const proto::ProtoObject* methodArgs[9];
                    methodArgs[0] = recv;
                    for (int i = 0; i < argcOp; ++i)
                        methodArgs[i + 1] = sendArgs[i];
                    pushFrame(sub, /*self=*/recv, capDict, methodArgs,
                              static_cast<unsigned int>(argcOp) + 1);
                    // `f` is now invalidated by the vector growth (when it
                    // reallocates). Continue the loop so the next iteration
                    // re-acquires frames_.back() and dispatches the callee
                    // from its pc=0.
                    continue;
                }
                // F5-M3: member-access semantics. If the attribute is not a
                // method (no __bc_ptr__) and is not a primitive marker (a
                // tagged SmallInteger with bit 62 set), then a unary send is
                // treated as a property read — the attribute is itself the
                // result. This is what lets `m Greeter` resolve the class
                // stored on a module wrapper (or any plain value attribute).
                // For non-unary sends, treating a value attribute as a method
                // is still an error.
                if (!attr->isInteger(ctx)) {
                    if (argcOp == 0) {
                        push(f, attr);
                        break;
                    }
                    throw std::runtime_error(
                        "non-primitive method in F2 (F3 work): " + selStr);
                }
                long long marker = attr->asLong(ctx);
                if (!(marker & (1LL << 62))) {
                    // Plain integer value stored as an attribute — same
                    // member-access rule as above.
                    if (argcOp == 0) {
                        push(f, attr);
                        break;
                    }
                    throw std::runtime_error("non-primitive method in F2 (F3 work)");
                }
                int primIdx = static_cast<int>(marker & ((1LL << 62) - 1));
                auto fn = rt_.registry().at(primIdx);
                // F6 v3 A note: this primitive call MAY itself end up
                // invoking invokeBlock() (e.g. ifTrue:, thenDo:, value)
                // which spins up a FRESH ExecutionEngine on the C++ stack.
                // That secondary engine has its own independent frames_
                // and does not interact with ours. The bounded primitive-
                // nesting depth makes that acceptable for F6 v3 A; the
                // unbounded user-method recursion was the actual target of
                // this refactor and is now gone.
                auto* result = fn(rt_, ctx, recv, sendArgs, argcOp);
                push(f, result ? result : PROTO_NONE);
                break;
            }
            case Op::JUMP:          f.pc += static_cast<std::size_t>(arg) * kInstrSize; break;
            case Op::JUMP_IF_TRUE: {
                auto* v = pop(f);
                if (v == PROTO_TRUE) f.pc += static_cast<std::size_t>(arg) * kInstrSize;
                break;
            }
            case Op::JUMP_IF_FALSE: {
                auto* v = pop(f);
                if (v == PROTO_FALSE) f.pc += static_cast<std::size_t>(arg) * kInstrSize;
                break;
            }
            case Op::PUSH_CAPTURED: {
                // arg = constant pool index of the (interned) symbol name.
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const proto::ProtoObject* capD = getCaptured(f);
                const proto::ProtoObject* val =
                    (capD && capD != PROTO_NONE)
                        ? capD->getAttribute(ctx, sym)
                        : nullptr;
                push(f, val ? val : PROTO_NONE);
                break;
            }
            case Op::STORE_CAPTURED: {
                if (f.sp == 0)
                    throw std::runtime_error("STORE_CAPTURED with empty stack");
                const proto::ProtoObject* val = pop(f);
                const proto::ProtoObject* capD = getCaptured(f);
                if (!capD || capD == PROTO_NONE)
                    throw std::runtime_error("STORE_CAPTURED without captured dict");
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                const_cast<proto::ProtoObject*>(capD)->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_GLOBAL: {
                // arg = constant pool index of the (interned) global name.
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                auto* val = g ? g->getAttribute(ctx, sym) : nullptr;
                if (!val || val == PROTO_NONE)
                    throw std::runtime_error("undefined global: " + nameStr);
                push(f, val);
                break;
            }
            case Op::STORE_GLOBAL: {
                if (f.sp == 0)
                    throw std::runtime_error("STORE_GLOBAL with empty stack");
                const proto::ProtoObject* val = pop(f);
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = ctx->fromUTF8String(nameStr.c_str())->asString(ctx);
                auto* g = rt_.globals();
                if (!g) throw std::runtime_error("globals not initialized");
                g->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_INSTVAR: {
                // F4-U5: read an instance variable from `self`. `self` is
                // populated by runWithArgs into slot 0 of locals (see the
                // user-method dispatch path in SEND_*). arg is the constant-
                // pool index of the (interned) inst-var name symbol.
                //
                // We store inst vars under a mangled key ("_iv_<name>") so
                // their storage does NOT collide with same-named method
                // selectors in the receiver's prototype chain. Without this,
                // `Counter >> value ^ value.` would have the message-send
                // `c value` find the integer attribute stored by STORE_INSTVAR
                // instead of the method installed on the class, since
                // getAttribute walks own-attrs first.
                const std::string& nameStr = f.m->constSymbol(arg);
                std::string mangled = "_iv_";
                mangled += nameStr;
                auto* sym = ctx->fromUTF8String(mangled.c_str())->asString(ctx);
                if (f.localCount == 0)
                    throw std::runtime_error("PUSH_INSTVAR with no self");
                const proto::ProtoObject* self = getLocal(f, 0);
                auto* val = (self && self != PROTO_NONE)
                    ? self->getAttribute(ctx, sym) : nullptr;
                push(f, val ? val : PROTO_NONE);
                break;
            }
            case Op::STORE_INSTVAR: {
                // F4-U5: write an instance variable on `self`. Mirror of
                // PUSH_INSTVAR; pops the value-to-store from the operand
                // stack and setAttribute's it under the mangled "_iv_<name>"
                // key on local-0 self. See PUSH_INSTVAR for rationale.
                if (f.sp == 0)
                    throw std::runtime_error("STORE_INSTVAR empty stack");
                const proto::ProtoObject* val = pop(f);
                const std::string& nameStr = f.m->constSymbol(arg);
                std::string mangled = "_iv_";
                mangled += nameStr;
                auto* sym = ctx->fromUTF8String(mangled.c_str())->asString(ctx);
                if (f.localCount == 0)
                    throw std::runtime_error("STORE_INSTVAR with no self");
                const proto::ProtoObject* self = getLocal(f, 0);
                if (!self || self == PROTO_NONE)
                    throw std::runtime_error("STORE_INSTVAR self is null");
                const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, sym, val);
                break;
            }
            default:
                throw std::runtime_error(
                    "ExecutionEngine: unimplemented opcode at pc=" +
                    std::to_string(f.pc - kInstrSize));
        }
    }
    // frames_ drained without hitting a RETURN at the top frame — this
    // can happen if a top-level module's bytes simply run out (the
    // implicit RETURN_TOP trailer is normally emitted by the compiler,
    // but be defensive).
    return PROTO_NONE;
    } catch (const FutureYield& y) {
        // F6 v3 C: cooperative yield. The wait primitive threw because we
        // are inside an actor handler AND the awaited future is still
        // pending. Snapshot the frame stack onto the actor and rethrow so
        // STRuntime::drainOne sees the yield and returns the worker to
        // the ready queue without resolving the message's future.
        //
        // The snapshot is taken BEFORE we clear frames_; restoreFrames
        // (used by the resume path) will rebuild the same shape later.
        const proto::ProtoObject* actor = rt_.currentActor();
        if (!actor) {
            // FutureYield outside an actor handler is a programming
            // error. Convert to a normal runtime_error rather than
            // letting an unmatched exception escape. Future>>wait only
            // throws when currentActor() != nullptr, so reaching here
            // would indicate a primitive misuse.
            throw std::runtime_error("FutureYield outside of actor handler");
        }

        // Symbols resolved fresh from the live ctx (not cached in statics):
        // protoCore interns symbols per-ProtoSpace, so a static binds to the
        // first runtime's space and dangles for every later STRuntime. The
        // matching drainOne resume path resolves the same names the same
        // way; consistency across both sites is what makes the suspended-
        // frame handoff observable.
        const proto::ProtoString* suspKey =
            ctx->fromUTF8String("__suspended_frame__")->asString(ctx);
        const proto::ProtoString* waitingOnKey =
            ctx->fromUTF8String("__waiting_on__")->asString(ctx);

        const proto::ProtoObject* snap = snapshotFrames(ctx);
        SCHED_DIAG("engine YIELD actor=" << actor
                   << " future=" << y.future()
                   << " frames=" << frames_.size());
        const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, suspKey, snap);
        if (y.future()) {
            const_cast<proto::ProtoObject*>(actor)->setAttribute(
                ctx, waitingOnKey, y.future());

            // Append the actor to the future's __waiters__ list under the
            // future's cv mutex so it cannot race with resolve/reject on
            // a different thread. The helper returns false if the future
            // had ALREADY settled between Future>>wait's state check and
            // our acquisition of the mutex; in that case the
            // resolveFutureFromDrain run that just finished took an
            // empty waiters snapshot, so we must schedule the actor
            // ourselves to make sure the resume path runs.
            //
            // We import the helper indirectly from future_prims.cpp; the
            // linker connects them.
            extern bool appendFutureWaiterLocked(
                proto::ProtoContext* ctx,
                const proto::ProtoObject* fut,
                const proto::ProtoObject* waiterActor);
            bool parked = appendFutureWaiterLocked(ctx, y.future(), actor);
            if (!parked) {
                // Race window: producer settled the future in the gap
                // between wait's mutex release and our re-acquisition.
                // Schedule the actor explicitly so the resume path
                // observes the settled state.
                rt_.schedule(ctx, actor);
            }
        }

        // Drop frames_ now that the snapshot owns the state; the engine
        // is single-shot and will not be reused. Rethrow so drainOne's
        // catch path runs.
        frames_.clear();
        throw;
    } catch (DebuggerHalt& h) {
        // F6 v3 A: enter the debugger session on the CURRENT top frame.
        // The legacy engine captured pc/stack/locals from local C++ vars;
        // we now read them from frames_.back() which holds equivalent
        // state. If frames_ is empty (shouldn't happen — the halt was
        // thrown from inside a primitive called by some active frame),
        // fall back to a synthetic empty frame to keep the debugger
        // contract.
        DebugFrame dframe;
        if (!frames_.empty()) {
            dframe = makeDebugFrame(frames_.back());
        }
        rt_.debugger().enterSession(rt_, std::move(dframe), h.reason());
        // After the session resumes (user typed c/s/n/f), fall back through
        // the outer while(true) and continue executing from the current pc.
        // The halt primitive pushes PROTO_NONE as its return value into the
        // caller's stack via the SEND_UNARY handler, but throwing aborted
        // that. Push PROTO_NONE now so the operand stack matches what the
        // compiler expects after a unary send.
        if (!frames_.empty()) {
            push(frames_.back(), PROTO_NONE);
        }
        continue;
    }
    } // outer while(true)
}

// ---------------------------------------------------------------------------
// F6 v3 B: snapshot/restore round-trip for cooperative yield.
//
// Encoding (one ProtoObject per frame, packaged into a ProtoList):
//   pc            -> ProtoInteger (long long via ctx->fromLong)
//   op_stack      -> ProtoList of operand-stack contents (oldest at index 0)
//   locals        -> ProtoList of local-slot contents; nullptr slots become
//                    PROTO_NONE so the list size equals locals.size()
//   self_obj      -> the frame's self, or PROTO_NONE
//   captured_dict -> the frame's captured-vars environment, or PROTO_NONE
//   m_ptr         -> ExternalPointer wrapping the const BytecodeModule*,
//                    finalizer = nullptr (modules are owned elsewhere)
//
// Keys are interned symbols cached in function-local statics so the symbol
// table is hit at most once per process per key.
// ---------------------------------------------------------------------------

const proto::ProtoObject*
ExecutionEngine::snapshotFrames(proto::ProtoContext* ctx) const {
    // Resolve fresh from the live ctx — symbols are interned per-ProtoSpace,
    // so a function-local static would dangle once the runtime that first
    // ran snapshotFrames is destroyed. restoreFrames resolves the identical
    // names; the two must agree within a runtime for the round trip to work.
    const proto::ProtoString* pcKey       = ctx->fromUTF8String("pc")->asString(ctx);
    const proto::ProtoString* opStackKey  = ctx->fromUTF8String("op_stack")->asString(ctx);
    const proto::ProtoString* localsKey   = ctx->fromUTF8String("locals")->asString(ctx);
    const proto::ProtoString* selfKey     = ctx->fromUTF8String("self_obj")->asString(ctx);
    const proto::ProtoString* capturedKey = ctx->fromUTF8String("captured_dict")->asString(ctx);
    const proto::ProtoString* mPtrKey     = ctx->fromUTF8String("m_ptr")->asString(ctx);

    const proto::ProtoList* result = ctx->newList();
    for (const Frame& fr : frames_) {
        // Each frame becomes a fresh mutable child of objectProto. This
        // mirrors how Object>>asActor builds the actor wrapper and how the
        // SEND fast-path builds message envelopes — see the
        // `bootstrap.objectProto->newChild(ctx, /*isMutable=*/true)` pattern
        // elsewhere in this file.
        auto* frameObj = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
            ->newChild(ctx, /*isMutable=*/true);

        // pc
        frameObj->setAttribute(
            ctx, pcKey,
            ctx->fromLong(static_cast<long long>(fr.pc)));

        // op_stack as ProtoList (preserve order: index 0 = bottom of stack).
        // F6 v3 E3: the live operand stack is the [0, sp) slice of the
        // frame's operand-stack slot region inside automaticLocals.
        const proto::ProtoList* opList = ctx->newList();
        for (unsigned int j = 0; j < fr.sp; ++j) {
            const proto::ProtoObject* v =
                ctx->getAutomaticLocal(opStackBase(fr) + j);
            opList = opList->appendLast(ctx, v ? v : PROTO_NONE);
        }
        frameObj->setAttribute(ctx, opStackKey, opList->asObject(ctx));

        // locals as ProtoList (size == localCount). F6 v3 E3: read every
        // local slot out of the frame's region.
        const proto::ProtoList* locList = ctx->newList();
        for (unsigned int j = 0; j < fr.localCount; ++j) {
            const proto::ProtoObject* v = getLocal(fr, j);
            locList = locList->appendLast(ctx, v ? v : PROTO_NONE);
        }
        frameObj->setAttribute(ctx, localsKey, locList->asObject(ctx));

        // self_obj — read from the frame's header slot.
        const proto::ProtoObject* selfV = getSelf(fr);
        frameObj->setAttribute(
            ctx, selfKey,
            selfV ? selfV : PROTO_NONE);

        // captured_dict — read from the frame's header slot.
        const proto::ProtoObject* capV = getCaptured(fr);
        frameObj->setAttribute(
            ctx, capturedKey,
            capV ? capV : PROTO_NONE);

        // m_ptr — borrow the BytecodeModule pointer; no finalizer because the
        // module is owned by STRuntime::loadedModules or by the original
        // top-level caller. const_cast is safe here: the snapshot only reads
        // the module through the const protoST::BytecodeModule* recovered on
        // restoreFrames.
        frameObj->setAttribute(
            ctx, mPtrKey,
            ctx->fromExternalPointer(
                const_cast<protoST::BytecodeModule*>(fr.m),
                /*finalizer=*/nullptr));

        result = result->appendLast(ctx, frameObj);
    }
    return result->asObject(ctx);
}

void
ExecutionEngine::restoreFrames(proto::ProtoContext* ctx,
                               const proto::ProtoObject* snapshot) {
    // Resolve fresh from the live ctx — see snapshotFrames for the
    // per-space interning rationale.
    const proto::ProtoString* pcKey       = ctx->fromUTF8String("pc")->asString(ctx);
    const proto::ProtoString* opStackKey  = ctx->fromUTF8String("op_stack")->asString(ctx);
    const proto::ProtoString* localsKey   = ctx->fromUTF8String("locals")->asString(ctx);
    const proto::ProtoString* selfKey     = ctx->fromUTF8String("self_obj")->asString(ctx);
    const proto::ProtoString* capturedKey = ctx->fromUTF8String("captured_dict")->asString(ctx);
    const proto::ProtoString* mPtrKey     = ctx->fromUTF8String("m_ptr")->asString(ctx);

    if (!snapshot)
        throw std::runtime_error("restoreFrames: snapshot is null");
    auto* asList = snapshot->asList(ctx);
    if (!asList)
        throw std::runtime_error("restoreFrames: snapshot is not a list");

    // F6 v3 E3: restoreFrames is the engine's entry point on the resume path
    // (drainOne calls it before resumeWith + continueRun). Establish the same
    // engine-context state runWithArgs would: bind ctx_, ensure the GC-traced
    // slot array is pre-sized, and anchor this engine's frame regions at the
    // current thread-local cursor so a nested engine cannot overlap them.
    ctx_ = ctx;
    ctx_->resizeAutomaticLocals(kSlotCapacity);

    frames_.clear();
    slotBase_ = g_slotCursor;

    const unsigned long n = asList->getSize(ctx);
    frames_.reserve(std::max<std::size_t>(64, static_cast<std::size_t>(n)));

    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* frameObj =
            asList->getAt(ctx, static_cast<int>(i));
        if (!frameObj)
            throw std::runtime_error("restoreFrames: null frame entry");

        // pc
        auto* pcVal = frameObj->getAttribute(ctx, pcKey);
        if (!pcVal || pcVal == PROTO_NONE)
            throw std::runtime_error("restoreFrames: missing pc");
        const std::size_t pc =
            static_cast<std::size_t>(pcVal->asLong(ctx));

        // op_stack
        auto* opStackVal = frameObj->getAttribute(ctx, opStackKey);
        auto* opList = opStackVal ? opStackVal->asList(ctx) : nullptr;
        if (!opList)
            throw std::runtime_error("restoreFrames: missing op_stack");
        const unsigned long opN = opList->getSize(ctx);

        // locals
        auto* locVal = frameObj->getAttribute(ctx, localsKey);
        auto* locList = locVal ? locVal->asList(ctx) : nullptr;
        if (!locList)
            throw std::runtime_error("restoreFrames: missing locals");
        const unsigned long locN = locList->getSize(ctx);

        // m_ptr
        auto* mPtrVal = frameObj->getAttribute(ctx, mPtrKey);
        if (!mPtrVal || mPtrVal == PROTO_NONE)
            throw std::runtime_error("restoreFrames: missing m_ptr");
        auto* ep = mPtrVal->asExternalPointer(ctx);
        if (!ep)
            throw std::runtime_error("restoreFrames: m_ptr is not an ExternalPointer");
        const BytecodeModule* m =
            static_cast<const BytecodeModule*>(ep->getPointer(ctx));
        if (!m)
            throw std::runtime_error("restoreFrames: m_ptr decoded to nullptr");

        // self_obj / captured_dict — PROTO_NONE sentinel kept as-is in slots.
        auto* selfVal = frameObj->getAttribute(ctx, selfKey);
        auto* capVal  = frameObj->getAttribute(ctx, capturedKey);

        // F6 v3 E3: allocate this frame's slot region. localCount must be at
        // least the snapshot's locals count AND the module's own ceiling, so
        // a resumed frame can store every local the body touches. maxStack
        // must hold the snapshotted operand stack.
        Frame fr;
        fr.m          = m;
        fr.pc         = pc;
        fr.localCount = std::max<unsigned int>(
            computeLocalCount(*m, 0),
            static_cast<unsigned int>(locN));
        fr.maxStack   = std::max<unsigned int>(
            kFrameMaxStk, static_cast<unsigned int>(opN));
        fr.sp         = static_cast<unsigned int>(opN);
        fr.baseSlot   = g_slotCursor;

        const unsigned int regionSize = frameRegionSize(fr);
        const unsigned int regionEnd  = fr.baseSlot + regionSize;
        if (regionEnd > kSlotCapacity)
            throw std::runtime_error("engine slot region exhausted");

        // Initialise the region, then write self/captured/locals/opStack.
        for (unsigned int s = fr.baseSlot; s < regionEnd; ++s)
            ctx_->setAutomaticLocal(s, PROTO_NONE);
        ctx_->setAutomaticLocal(fr.baseSlot + 0,
                                capVal ? capVal : PROTO_NONE);
        ctx_->setAutomaticLocal(fr.baseSlot + 1,
                                selfVal ? selfVal : PROTO_NONE);
        for (unsigned long j = 0; j < locN; ++j) {
            const proto::ProtoObject* v = locList->getAt(ctx, static_cast<int>(j));
            ctx_->setAutomaticLocal(localsBase(fr) + static_cast<unsigned int>(j),
                                    v ? v : PROTO_NONE);
        }
        for (unsigned long j = 0; j < opN; ++j) {
            const proto::ProtoObject* v = opList->getAt(ctx, static_cast<int>(j));
            ctx_->setAutomaticLocal(opStackBase(fr) + static_cast<unsigned int>(j),
                                    v ? v : PROTO_NONE);
        }

        g_slotCursor += regionSize;
        frames_.push_back(fr);
    }
}

// ---------------------------------------------------------------------------
// F6 v3 C+D: resume entry points.
//
// resumeWith is called after restoreFrames has populated frames_ from a
// snapshot. It primes the topmost frame's operand stack with the result the
// yielded `Future>>wait` would have returned synchronously:
//
//   * resolved → push `value` (or PROTO_NONE if null) onto opStack.
//   * rejected → throw std::runtime_error so the engine's normal
//     in-flight exception path takes over from continueRun. The error
//     surfaces in drainOne's std::exception catch, which rejects the
//     message-level future with the same string a synchronous
//     `wait` on a rejected future would have produced.
//
// continueRun then re-enters runLoop. The dispatch loop reads frames_.back()
// and resumes at the pc the snapshot was taken on — which the engine
// already advanced past the SEND of `wait` (pc is incremented BEFORE the
// switch). So the next bytecode executed is whatever was emitted AFTER
// the `wait` call site, with the resumed value sitting on top of the
// operand stack exactly as the SEND handler would have left it.
// ---------------------------------------------------------------------------

void
ExecutionEngine::resumeWith(proto::ProtoContext* ctx,
                            const proto::ProtoObject* value,
                            const proto::ProtoObject* error) {
    if (error) {
        std::string msg = "Future rejected: ";
        // Best-effort string materialisation — error is the same object the
        // future stored under __error__, which Future>>wait would have
        // formatted via asString. We mirror that here so the rejection
        // surface is identical between the synchronous-wait and resume
        // paths.
        auto* es = error->asString(ctx);
        if (es) msg += es->toStdString(ctx);
        throw std::runtime_error(msg);
    }
    if (frames_.empty()) {
        // No frames to resume — restoreFrames must have been called with an
        // empty snapshot. There's nothing to push the value onto; treat as
        // a no-op so continueRun returns PROTO_NONE.
        return;
    }
    // F6 v3 E3: push onto the top frame's GC-traced operand-stack region.
    push(frames_.back(), value ? value : PROTO_NONE);
}

const proto::ProtoObject*
ExecutionEngine::continueRun(proto::ProtoContext* ctx) {
    // Same dispatch loop as runWithArgs; frames_ has been primed by
    // restoreFrames + resumeWith.
    return runLoop(ctx);
}

} // namespace protoST
