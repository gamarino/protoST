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
#include "NonLocalReturn.h"
#include "SchedDiag.h"
#include "Opcodes.h"
#include "TransientPin.h"
#include "NativeExceptionBridge.h"
#include "debugger/DebuggerRuntime.h"
#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "protoCore.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoST {

// COL-a: build a fresh Array instance wrapping the given protoCore ProtoList.
// Defined in collection_prims.cpp; used here by the MAKE_ARRAY opcode handler.
const proto::ProtoObject* makeArrayInstance(STRuntime& rt,
                                            proto::ProtoContext* ctx,
                                            const proto::ProtoList* data);

namespace {

// Track 1 slice 1: process-global frame-id allocator. Every frame pushed by
// ANY ExecutionEngine on ANY thread takes the next id. 0 is the reserved
// "none" sentinel (a default-constructed Frame, or "no home recorded"), so
// the counter starts at 1. Global ids let a block's homeFrameId identify its
// home method unambiguously across engine boundaries and across the
// snapshot/restore that backs cooperative yield.
std::atomic<unsigned long> g_nextFrameId{1};

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
    // BL-2: the scan must decode EXTEND prefixes exactly as the engine does,
    // or a PUSH_LOCAL / STORE_LOCAL with a slot index > 255 would be read as
    // its low byte only and the frame's slot region would be undersized.
    for (std::size_t pc = 0; pc + 1 < bytes.size(); ) {
        Op op = static_cast<Op>(bytes[pc]);
        unsigned int arg = bytes[pc + 1];
        pc += kInstrSize;
        while (op == Op::EXTEND && pc + 1 < bytes.size()) {
            op  = static_cast<Op>(bytes[pc]);
            arg = (arg << 8) | bytes[pc + 1];
            pc += kInstrSize;
        }
        if (op == Op::PUSH_LOCAL || op == Op::STORE_LOCAL) {
            if (!sawSlot || arg > maxSlot) { maxSlot = arg; sawSlot = true; }
        }
    }
    unsigned int needed = sawSlot ? (maxSlot + 1) : 0;
    needed = std::max(needed, argc);
    needed = std::max(needed, kMinLocals);
    return needed;
}

} // namespace

// F6 v3 E5: definition of the thread-local transient-pin scratch cursor
// declared `extern` in TransientPin.h. Counts CONSUMED scratch slots; the
// scratch region fills the top kScratchSlots slots of automaticLocals from
// the top downward, while frame regions fill from slot 0 upward. The two
// cursors grow toward each other; pushFrame asserts they never collide.
thread_local unsigned int g_scratchCursor = 0;

// Compile-time consistency: the scratch geometry in TransientPin.h must match
// this engine's slot capacity (8192), or a primitive would pin into slots the
// engine believes are free frame storage. ExecutionEngine::kSlotCapacity is
// asserted equal to kEngineSlotCapacity inside pushFrame (a member function
// with access to the private constant).
static_assert(kEngineSlotCapacity == 8192,
              "TransientPin scratch geometry assumes an 8192-slot engine context");

// D8 (MNT-b2): per-thread registry of live ExecutionEngine instances.
//
// Engines nest strictly LIFO on the C++ stack — invokeBlock and user-method
// dispatch each spin a fresh nested engine, run it to completion, and destroy
// it before their own caller continues. Every engine registers itself here for
// its whole lifetime (ctor → push_back, dtor → erase). `homeFrameAlive` scans
// the registry to answer "is the method activation with this frameId still on
// the call chain anywhere on this thread?" — a `^` whose home is NOT found is a
// dead-home non-local return and must signal a catchable `BlockCannotReturn`
// rather than throw an uncatchable C++ exception that would unwind past
// `on:do:` handlers before anyone could convert it.
static thread_local std::vector<ExecutionEngine*> g_liveEngines;

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

// D8 (MNT-b2): registry-managed lifetime. Each engine joins g_liveEngines on
// construction and leaves on destruction. The vector grows/shrinks LIFO with
// the C++ engine nesting, so an erase is always of the back element in the
// common case; a defensive search-and-erase handles any non-LIFO surprise.
ExecutionEngine::ExecutionEngine(STRuntime& rt) : rt_(rt) {
    g_liveEngines.push_back(this);
}

ExecutionEngine::~ExecutionEngine() {
    for (std::size_t i = g_liveEngines.size(); i-- > 0; ) {
        if (g_liveEngines[i] == this) {
            g_liveEngines.erase(g_liveEngines.begin() +
                                static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

bool
ExecutionEngine::homeFrameAlive(unsigned long frameId) {
    if (frameId == 0) return false;
    for (ExecutionEngine* e : g_liveEngines) {
        for (const Frame& f : e->frames_) {
            if (f.frameId == frameId) return true;
        }
    }
    return false;
}

void
ExecutionEngine::pushFrame(const BytecodeModule* m,
                           const proto::ProtoObject* self,
                           const proto::ProtoObject* captured,
                           const proto::ProtoObject* const* args,
                           unsigned int argc,
                           unsigned long homeFrameId) {
    Frame fr;
    fr.m          = m;
    fr.pc         = 0;
    fr.localCount = computeLocalCount(*m, argc);
    fr.maxStack   = kFrameMaxStk;
    fr.sp         = 0;
    // Track 1 slice 1: assign frame identity. homeFrameId == 0 means "I am my
    // own home" — a method or top-level frame returns from itself. A non-zero
    // homeFrameId is a block frame inheriting its creating method's home, so
    // an `^` inside it returns from that method.
    fr.frameId     = g_nextFrameId.fetch_add(1, std::memory_order_relaxed);
    fr.homeFrameId = (homeFrameId == 0) ? fr.frameId : homeFrameId;
    // F6 v3 E5: the engine's slot capacity and the TransientPin scratch
    // geometry must agree — pushFrame is a member function so it can see the
    // private constant. Frame regions occupy [0, kFrameRegionLimit); the top
    // kScratchSlots slots are reserved for transient pins.
    static_assert(kSlotCapacity == kEngineSlotCapacity,
                  "engine kSlotCapacity must equal TransientPin kEngineSlotCapacity");

    // Allocate this frame's region from the shared thread-local cursor so it
    // never overlaps an outer (nested-engine) frame's region.
    fr.baseSlot = g_slotCursor;
    const unsigned int regionSize = frameRegionSize(fr);
    const unsigned int regionEnd  = fr.baseSlot + regionSize;
    // F6 v3 E5: frame regions must not grow into the transient-pin scratch
    // region at the top of automaticLocals. The previous bound was
    // kSlotCapacity; it is now kFrameRegionLimit (== kSlotCapacity -
    // kScratchSlots) so the scratch slots are never aliased by frame storage.
    if (regionEnd > kFrameRegionLimit)
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
                             const proto::ProtoObject* capturedDict,
                             unsigned long homeFrameId) {
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
              static_cast<unsigned int>(argc < 0 ? 0 : argc),
              homeFrameId);

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

// F8-4: snapshot the whole `frames_` stack. The top-level DebugFrame mirrors
// the innermost frame (so single-frame consumers are unaffected); its
// `callStack` carries every frame oldest-first, current frame last, each with
// its own frameDepth set to its index in the stack.
DebugFrame
ExecutionEngine::makeDebugStack() const {
    DebugFrame top;
    if (frames_.empty())
        return top;
    top = makeDebugFrame(frames_.back());
    top.callStack.reserve(frames_.size());
    for (size_t i = 0; i < frames_.size(); ++i) {
        DebugFrame d = makeDebugFrame(frames_[i]);
        d.frameDepth = static_cast<int>(i);
        top.callStack.push_back(std::move(d));
    }
    top.frameDepth = static_cast<int>(frames_.size()) - 1;
    return top;
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
            rt_.debugger().enterSession(rt_, makeDebugStack(), "step");
            // Session may have updated mode (e.g. user typed 'c'); fall
            // through to dispatch the next instruction.
        }

        // F2 location breakpoint: halt BEFORE executing the instruction at pc.
        if (rt_.debugger().attached() && rt_.debugger().breakpoints().isSet(f.m, f.pc)) {
            rt_.debugger().enterSession(rt_, makeDebugStack(),
                                        "breakpoint");
        }

        // BL-2: wide-operand decode. An instruction is one or more EXTEND
        // prefix words followed by the real opcode word. Each EXTEND carries
        // the next-most-significant 8 bits of the operand; the engine shifts
        // the accumulator left 8 and ORs in each prefix, then ORs the real
        // word's low byte. A plain (non-wide) instruction skips the loop and
        // `arg` is just bytes[pc+1], identical to the pre-BL-2 behaviour.
        //
        // `f.pc` is advanced past EVERY consumed word so it always lands on
        // the next instruction boundary — snapshot/restore (which stores the
        // raw pc) therefore still resumes correctly.
        Op op = static_cast<Op>(bytes[f.pc]);
        unsigned int arg = bytes[f.pc + 1];
        f.pc += kInstrSize;
        while (op == Op::EXTEND) {
            if (f.pc + 1 >= bytes.size())
                throw std::runtime_error(
                    "ExecutionEngine: truncated EXTEND prefix");
            op  = static_cast<Op>(bytes[f.pc]);
            arg = (arg << 8) | bytes[f.pc + 1];
            f.pc += kInstrSize;
        }

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
            case Op::RETURN_TOP: {
                // RETURN_TOP is the compiler's implicit trailer for a block
                // body and for a top-level module. It is ALWAYS a local
                // return: pop exactly this frame and hand its top-of-stack
                // to the caller frame (or back to the C++ caller of
                // run/runWithArgs when this was the last frame). Falling off
                // the end of a block therefore returns only from the block.
                const proto::ProtoObject* r =
                    f.sp == 0 ? PROTO_NONE : peek(f);
                popFrame();
                if (frames_.empty()) return r;
                push(frames_.back(), r);
                // Reference `f` is now dangling — continue the loop so the
                // next iteration re-acquires frames_.back().
                continue;
            }
            case Op::RETURN: {
                // Op::RETURN is emitted by an explicit `^expr` and by a
                // method's implicit trailer. Track 1 slice 1 makes it
                // home-aware.
                const proto::ProtoObject* r =
                    f.sp == 0 ? PROTO_NONE : peek(f);

                if (f.homeFrameId == f.frameId) {
                    // A method or top-level frame returning normally: the
                    // home IS this frame. Local return — identical to
                    // RETURN_TOP.
                    popFrame();
                    if (frames_.empty()) return r;
                    push(frames_.back(), r);
                    continue;
                }

                // A block frame running `^expr` — non-local return. Find the
                // home method activation in THIS engine's frame stack.
                std::size_t homeIdx = frames_.size();
                for (std::size_t i = frames_.size(); i-- > 0; ) {
                    if (frames_[i].frameId == f.homeFrameId) {
                        homeIdx = i;
                        break;
                    }
                }
                if (homeIdx != frames_.size()) {
                    // Home found locally: unwind every frame from the top
                    // down to AND INCLUDING the home frame. popFrame()
                    // rewinds g_slotCursor per pop, so the cursor stays
                    // correct across the multi-frame unwind.
                    while (frames_.size() > homeIdx)
                        popFrame();
                    if (frames_.empty()) return r;
                    push(frames_.back(), r);
                    continue;
                }

                // The home is not in THIS engine's frames_. Two cases:
                //
                //  * D8 (MNT-b2): the home method has ALREADY RETURNED — a
                //    "dead home". `homeFrameAlive` scans every live engine on
                //    this thread; if none holds the home activation, the `^`
                //    can never reach a live method. Previously this surfaced
                //    only later, as an uncatchable `std::runtime_error` at the
                //    outermost `runWithArgs` — by which point any `on:do:` on
                //    the path had already popped its handler. Signal a
                //    catchable `BlockCannotReturn` (a subclass of `Error`)
                //    HERE instead, while the handler stack is still intact, so
                //    `[ … ] on: Error do: [:e| …]` catches it.
                //
                //  * The home lives in an OUTER engine — a legitimate
                //    non-local return across an invokeBlock boundary. Throw
                //    NonLocalReturn so the C++ stack unwinds (running RAII
                //    guards) up to the engine that owns the home frame.
                if (!homeFrameAlive(f.homeFrameId)) {
                    auto* res = signalErrorOfClass(
                        rt_, ctx, rt_.bootstrap().blockCannotReturnProto,
                        "non-local return: home method has already returned");
                    // BlockCannotReturn is non-resumable, so signalErrorOfClass
                    // normally threw (UnwindToHandler to a catching `on:do:`,
                    // or UnhandledSTException at top level). If a handler did
                    // `resume:` anyway, treat the resumed value as this `^`'s
                    // result and continue from the block.
                    push(f, res ? res : PROTO_NONE);
                    break;
                }
                throw NonLocalReturn(f.homeFrameId, r);
            }
            case Op::PUSH_BLOCK: {
                // arg = block index inside f.m->blocks(). Create a fresh
                // BlockClosure (a mutable child of the Block prototype) that
                // carries an opaque pointer to the sub-module under
                // attribute "__bc_ptr__".
                auto* blkProto = rt_.bootstrap().blockProto;
                auto* block = blkProto->newChild(ctx, /*isMutable=*/true);
                // F6 v3 E5: `block` is a fresh mutable object held in a C++
                // local across the fromUTF8String key interning (first call
                // allocates), fromLong (allocates), and two setAttribute
                // calls on a mutable object (each allocates a sparse-list
                // node). Until `push(f, block)` lands it in a GC-traced frame
                // slot it is reachable from nowhere the collector traces.
                TransientPin pinBlock(ctx, block);
                const proto::ProtoString* bcKey =
                    rt_.bootstrap().sym.bcPtr;
                const proto::ProtoString* capKey =
                    rt_.bootstrap().sym.captured;
                const proto::ProtoString* homeKey =
                    proto::ProtoString::createSymbol(ctx, "__home_frame__");
                const proto::ProtoString* blkSelfKey =
                    proto::ProtoString::createSymbol(ctx, "__block_self__");
                auto* bcPtrObj = ctx->fromLong(
                    reinterpret_cast<long long>(&f.m->block(arg)));
                // `bcPtrObj` is held across the setAttribute below — pin it.
                TransientPin pinBcPtr(ctx, bcPtrObj);
                block->setAttribute(ctx, bcKey, bcPtrObj);
                // F3-C5: stash the current frame's captured dict so the
                // block can resolve free variables back to the outer scope
                // at invocation time.
                const proto::ProtoObject* capD = getCaptured(f);
                block->setAttribute(
                    ctx, capKey,
                    (capD && capD != PROTO_NONE) ? capD : PROTO_NONE);
                // Track 1 slice 1: stamp the block with the HOME method
                // activation. We store the creating frame's homeFrameId
                // (NOT its frameId): a block created directly in a method
                // gets that method's home, and a block created inside an
                // outer block inherits the same method home the outer block
                // already carries — so `^` from any nesting depth targets
                // the method. `f.homeFrameId` gives both cases.
                auto* homeObj = ctx->fromLong(
                    static_cast<long long>(f.homeFrameId));
                // F6 v3 E5 discipline: `homeObj` is held across setAttribute
                // on the mutable block object (which allocates) — pin it.
                TransientPin pinHome(ctx, homeObj);
                block->setAttribute(ctx, homeKey, homeObj);
                // CLO Part 1: stamp the block with the `self` of the frame
                // that creates it (the header self-slot, baseSlot+1). For a
                // block created directly in a method this is the method's
                // receiver; for a block nested inside another block the
                // creating frame's self is already the inherited self, so the
                // stamp is transitive. Block invocation then builds the block
                // frame with this self instead of PROTO_NONE, making
                // `self` / PUSH_INSTVAR work inside method-level blocks.
                const proto::ProtoObject* creatorSelf = getSelf(f);
                if (!creatorSelf) creatorSelf = PROTO_NONE;
                // `creatorSelf` is an already-rooted frame value, but pin it
                // for symmetry across the setAttribute (which allocates).
                TransientPin pinBlkSelf(ctx, creatorSelf);
                block->setAttribute(ctx, blkSelfKey, creatorSelf);
                push(f, block);
                break;
            }
            case Op::SEND_UNARY:
            case Op::SEND_BINARY:
            case Op::SEND_KEYWORD:
            case Op::SEND_SUPER: {
                // pop N args (0 for unary, 1 for binary, count from selector
                // for keyword / super)
                int argcOp = (op == Op::SEND_UNARY)  ? 0
                           : (op == Op::SEND_BINARY) ? 1
                           : /* keyword / super */ 0;
                const std::string& selStr = f.m->constSymbol(arg);
                if (op == Op::SEND_KEYWORD || op == Op::SEND_SUPER)
                    for (char c : selStr) if (c == ':') ++argcOp;
                // BL-1: SEND_SUPER is an explicit super-send opcode (the
                // current compiler routes `super foo` through PUSH_SUPER +
                // SEND_*, but honour the dedicated opcode too).
                if (op == Op::SEND_SUPER) f.superPending = true;
                // BL-1: capture & clear the super flag for this send. It is
                // armed by PUSH_SUPER (or SEND_SUPER above) and applies to
                // exactly one send.
                const bool isSuperSend = f.superPending;
                f.superPending = false;

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

                // Selector symbol — interned once and cached on the module
                // (constSym), not re-interned on every send.
                auto* selSym = f.m->constSym(ctx, arg);
                // `selSym` is an interned (perennial) ProtoString held in
                // a bare C++ local for the ENTIRE remainder of this handler —
                // across getAttribute walks, the actor-path allocations
                // (newFuture / newChild / newList / appendLast / setAttribute),
                // primitive dispatch, and pushFrame. None of those root it
                // (the receiver and popped args stay rooted via their frame
                // slots, but `selSym` is brand new and reachable from nowhere
                // the GC traces). A GC cycle between here and the last use
                // would reclaim it → use-after-free, observed as the deep-
                // chain `doesNotUnderstand` (the known #5 bug). Pin it.
                TransientPin pinSelSym(
                    ctx, reinterpret_cast<const proto::ProtoObject*>(selSym));

                // F6-A4: actor fast-path. If the receiver is an actor (i.e.
                // carries a __wrapped__ attribute installed by Object>>asActor),
                // we bypass the synchronous dispatch entirely. The send is
                // converted into a Message envelope, enqueued on the actor's
                // mailbox, the actor is scheduled, and a fresh pending Future
                // is pushed onto the operand stack as the apparent result of
                // the send. The actual method execution happens later when
                // STRuntime::drainOne pulls a message from the mailbox.
                if (rt_.isActor(ctx, recv)) {
                    const proto::ProtoString* mbKey =
                        rt_.bootstrap().sym.mailbox;
                    const proto::ProtoString* msgSelKey =
                        rt_.bootstrap().sym.selector;
                    const proto::ProtoString* msgArgsKey =
                        rt_.bootstrap().sym.args;
                    const proto::ProtoString* msgFutKey =
                        rt_.bootstrap().sym.future;

                    // Allocate a fresh pending Future.
                    // F6 v3 E5: `fut` is held in a C++ local across newChild,
                    // newList, the appendLast loop, three setAttribute calls
                    // on mutable objects, the CAS-retry mailbox append and
                    // schedule() — all of which allocate. Pin it.
                    auto* fut = const_cast<proto::ProtoObject*>(rt_.newFuture(ctx));
                    TransientPin pinFut(ctx, fut);

                    // Build the message envelope (a fresh mutable child of objectProto).
                    // F6 v3 E5: `msg` is held across the appendLast loop, the
                    // setAttribute calls, the RMW and schedule() — pin it.
                    auto* msg = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
                        ->newChild(ctx, /*isMutable=*/true);
                    TransientPin pinMsg(ctx, msg);

                    // Build args ProtoList by FIFO appendLast of each arg.
                    // F6 v3 E5: `argsList` is rebuilt by appendLast each turn
                    // (structural-sharing COW) and then held across the
                    // setAttribute calls. A single pin tracks the latest list
                    // value via reset(); the previous values become garbage
                    // immediately and need no rooting.
                    auto* argsList = ctx->newList();
                    TransientPin pinArgsList(
                        ctx, reinterpret_cast<const proto::ProtoObject*>(argsList));
                    for (int i = 0; i < argcOp; ++i) {
                        argsList = argsList->appendLast(ctx, sendArgs[i]);
                        pinArgsList.reset(
                            reinterpret_cast<const proto::ProtoObject*>(argsList));
                    }

                    msg->setAttribute(ctx, msgSelKey, reinterpret_cast<const proto::ProtoObject*>(selSym));
                    msg->setAttribute(ctx, msgArgsKey, argsList->asObject(ctx));
                    msg->setAttribute(ctx, msgFutKey, fut);

                    // Append to the actor's mailbox with a lock-free
                    // compare-and-swap retry. The mailbox is held under the
                    // actor's __mailbox__ attribute; a concurrent drainOne pop
                    // or a parallel SEND is a competing read-modify-write.
                    // Each try reads the current mailbox, builds the
                    // FIFO-extended list, and publishes it only if __mailbox__
                    // still holds exactly the snapshot we read
                    // (ProtoObject::setAttributeIfEqual — protoCore's atomic
                    // attribute CAS). A lost CAS means another writer won
                    // since our read; re-read and retry. This replaces the
                    // former per-actor std::mutex: no language-level lock, so
                    // no GC-safe acquire and no way to stall the STW quorum.
                    //
                    // F6 v3 E5: `mailbox`, `newMailbox` and `newMbObj` are
                    // transients reachable from nothing the GC traces, held
                    // across allocating calls — pin each for the iteration.
                    for (;;) {
                        const proto::ProtoObject* mbObj =
                            recv->getOwnAttributeDirect(ctx, mbKey);
                        auto* mailbox = (mbObj && mbObj != PROTO_NONE)
                            ? mbObj->asList(ctx) : ctx->newList();
                        TransientPin pinMailbox(
                            ctx, reinterpret_cast<const proto::ProtoObject*>(mailbox));
                        auto* newMailbox = mailbox->appendLast(ctx, msg);
                        TransientPin pinNewMailbox(
                            ctx, reinterpret_cast<const proto::ProtoObject*>(newMailbox));
                        const proto::ProtoObject* newMbObj =
                            newMailbox->asObject(ctx);
                        TransientPin pinNewMbObj(ctx, newMbObj);
                        if (const_cast<proto::ProtoObject*>(recv)
                                ->setAttributeIfEqual(ctx, mbKey, mbObj, newMbObj))
                            break;
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
                    const proto::ProtoString* recvBcKey =
                        rt_.bootstrap().sym.bcPtr;
                    const proto::ProtoString* recvCapKey =
                        rt_.bootstrap().sym.captured;
                    const proto::ProtoString* recvHomeKey =
                        proto::ProtoString::createSymbol(ctx, "__home_frame__");
                    const proto::ProtoString* recvBlkSelfKey =
                        proto::ProtoString::createSymbol(ctx, "__block_self__");
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

                            // Track 1 slice 1: read the block's home method
                            // activation, stamped by PUSH_BLOCK. The block
                            // frame inherits it as its homeFrameId so an
                            // `^expr` inside the block returns from that
                            // method, not just from the block. Absent only
                            // for blocks not built by PUSH_BLOCK — fall back
                            // to 0 ("own home", a plain local return).
                            unsigned long blkHome = 0;
                            auto* homeObj =
                                recv->getAttribute(ctx, recvHomeKey);
                            if (homeObj && homeObj != PROTO_NONE)
                                blkHome = static_cast<unsigned long>(
                                    homeObj->asLong(ctx));

                            // CLO Part 1: blocks bind args into locals
                            // 0..argcOp-1. The block frame's `self` (header
                            // slot, baseSlot+1) is the `self` stamped onto
                            // the block by PUSH_BLOCK as `__block_self__` —
                            // the receiver of the method the block was
                            // textually created in. PUSH_SELF / PUSH_INSTVAR
                            // inside the block read that header slot and so
                            // resolve to the enclosing method's receiver.
                            // Absent only for blocks not built by PUSH_BLOCK
                            // — fall back to PROTO_NONE.
                            const proto::ProtoObject* blkSelf =
                                recv->getAttribute(ctx, recvBlkSelfKey);
                            if (!blkSelf || blkSelf == PROTO_NONE)
                                blkSelf = PROTO_NONE;
                            pushFrame(sub, /*self=*/blkSelf, capDict,
                                      sendArgs,
                                      static_cast<unsigned int>(argcOp),
                                      blkHome);
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
                //
                // BL-1: `super` sends. The receiver value (`recv`) is the
                // same object as `self` — it is still bound into the callee's
                // local 0 and passed to primitives. What differs is WHERE the
                // method lookup begins: not at `recv` itself, but one level
                // above the class that defines the currently executing
                // method (f.m->definingClass()). We resolve that class via
                // globals, take its first parent, and look the selector up
                // from there. This correctly skips an override on the
                // receiver's own class while still finding the inherited
                // implementation.
                const proto::ProtoObject* lookupFrom = recv;
                // T3-b: when a `super` send resolves the inherited method by
                // a multi-parent depth-first walk, the resolved attribute is
                // captured here so the shared lookup below uses it directly
                // rather than re-running getAttribute from a single node.
                bool superResolved = false;
                const proto::ProtoObject* superResolvedAttr = nullptr;
                if (isSuperSend) {
                    const std::string& defCls = f.m->definingClass();
                    if (defCls.empty())
                        throw std::runtime_error(
                            "super send outside a method body");
                    // T3-a: resolve the defining class by IDENTITY, not by
                    // re-resolving its name through the global namespace.
                    //
                    // The defining class is recorded by the compiler only as a
                    // *name* (BL-1). A name lookup through `globals()` breaks the
                    // moment the defining class is not a top-level global —
                    // notably a class defined inside an imported module: the
                    // module's classes are attributes of the module object, not
                    // globals of the importing program. So a `super` send inside
                    // such a module's own method would fail to resolve.
                    //
                    // Instead, walk the receiver's own prototype chain (which is
                    // built from real object identities — `FastCounter` ->
                    // `Counter` -> `Object`, crossing module boundaries freely)
                    // and find the chain entry whose `__class_name__` equals the
                    // defining class name. That entry IS the defining class
                    // object; its first parent is the superclass where the
                    // inherited implementation lives. This is robust regardless
                    // of where the class was defined.
                    const proto::ProtoString* classNameSym =
                        proto::ProtoString::createSymbol(ctx, "__class_name__");
                    const proto::ProtoObject* defClsObj = nullptr;
                    {
                        // T3-b: bounded depth-first, left-to-right walk over
                        // the FULL parent list, starting at the receiver. With
                        // single inheritance this follows parent 0, identical
                        // to the pre-T3-b behaviour. With multiple parents
                        // (a `uses:` class) the defining class may be reached
                        // through any parent subtree — a mixin's own method
                        // doing a `super` send — so every parent must be
                        // searched, in resolution order.
                        std::vector<const proto::ProtoObject*> stack;
                        stack.push_back(recv);
                        for (int hops = 0; !stack.empty() && hops < 4096;
                             ++hops) {
                            const proto::ProtoObject* node = stack.back();
                            stack.pop_back();
                            if (!node || node == PROTO_NONE) continue;
                            auto* own =
                                node->getOwnAttributeDirect(ctx, classNameSym);
                            if (own && own != PROTO_NONE) {
                                auto* nameStr = own->asString(ctx);
                                if (nameStr &&
                                    defCls == nameStr->toStdString(ctx)) {
                                    defClsObj = node;
                                    break;
                                }
                            }
                            auto* ps = node->getParents(ctx);
                            if (ps) {
                                // Push parents in reverse so the depth-first
                                // pop order is left-to-right (parent 0 first).
                                unsigned long sz = ps->getSize(ctx);
                                for (unsigned long k = sz; k > 0; --k)
                                    stack.push_back(
                                        ps->getAt(ctx,
                                                  static_cast<int>(k - 1)));
                            }
                        }
                    }
                    if (!defClsObj || defClsObj == PROTO_NONE) {
                        // Fallback: the receiver's chain does not name the
                        // defining class (e.g. a class object used directly as
                        // receiver, or a built-in). Resolve through globals as
                        // before so existing behaviour is preserved.
                        auto* clsSym = proto::ProtoString::createSymbol(
                            ctx, defCls.c_str());
                        TransientPin pinClsSym(
                            ctx, reinterpret_cast<const proto::ProtoObject*>(
                                     clsSym));
                        auto* g = rt_.globals();
                        defClsObj = g ? g->getAttribute(ctx, clsSym) : nullptr;
                    }
                    if (!defClsObj || defClsObj == PROTO_NONE)
                        throw std::runtime_error(
                            "super: cannot resolve defining class " + defCls);
                    // T3-b: `super` across multiple parents. Single
                    // inheritance has one parent; the defining class then has
                    // exactly one place the inherited method can live, and the
                    // pre-T3-b behaviour (search parent 0) is unchanged.
                    //
                    // With multiple parents (a class assembled with `uses:`
                    // mixins) the inherited implementation may live in ANY of
                    // the defining class's parent subtrees. `super` must take
                    // the NEXT class in the resolution order after the
                    // defining class: depth-first, left-to-right over
                    // `getParents` — the primary superclass subtree first,
                    // then each mixin subtree in listed order. The diamond
                    // case (a selector reachable via two parents) resolves to
                    // the first in this order.
                    //
                    // `getAttribute` already performs the depth-first walk of
                    // a single subtree (the node itself plus its parents), so
                    // searching each parent in order via `getAttribute` and
                    // taking the first non-nil hit yields exactly that order.
                    auto* parents = defClsObj->getParents(ctx);
                    if (!parents || parents->getSize(ctx) == 0)
                        throw std::runtime_error(
                            "super: class " + defCls + " has no superclass");
                    const proto::ProtoObject* superAttr = nullptr;
                    unsigned long pcount = parents->getSize(ctx);
                    for (unsigned long pi = 0; pi < pcount; ++pi) {
                        const proto::ProtoObject* parent =
                            parents->getAt(ctx, static_cast<int>(pi));
                        if (!parent || parent == PROTO_NONE) continue;
                        const proto::ProtoObject* hit =
                            parent->getAttribute(ctx, selSym);
                        if (hit && hit != PROTO_NONE) {
                            superAttr = hit;
                            break;
                        }
                    }
                    // Reaching here with no hit means no parent subtree
                    // defines the selector — fall through to `doesNotUnderstand`
                    // exactly as a normal failed lookup would. Anchor the
                    // lookup at parent 0 so the unresolved path below still has
                    // a coherent `lookupFrom` (its getAttribute also returns
                    // nil, so the doesNotUnderstand signal fires).
                    superResolvedAttr = superAttr;
                    superResolved = true;
                    lookupFrom = parents->getAt(ctx, 0);
                }
                auto* attr = superResolved
                                 ? superResolvedAttr
                                 : lookupFrom->getAttribute(ctx, selSym);
                // D5 (MNT-b2): class-side / instance-side isolation. A method
                // installed by `ClassName class >> sel` carries the
                // `__class_side__` marker (stamped by __installClassMethod:as:).
                // Such a method must NOT be reachable from an instance — only
                // from a class object. The receiver is a class object when it
                // owns `__class_name__` as a DIRECT own attribute (every user
                // class is stamped with `__setClassName:`; a built-in class
                // prototype is stamped at bootstrap). An instance merely
                // inherits that attribute through its chain, so it does not own
                // it. When a class-side method resolves for an instance
                // receiver, drop it — the send then falls through to the
                // `doesNotUnderstand` path below, signalling
                // `MessageNotUnderstood`, exactly as the spec's class/instance
                // protocol split requires.
                //
                // Only this one direction is enforced: an instance-side method
                // sent to a class object is still allowed, because the built-in
                // class prototypes (Array, Error, ...) deliberately double as
                // both the class object and the instance-behaviour holder, and
                // selectors such as `Array new:` rely on that. A `__class_side__`
                // marker is only ever stamped on USER class-side methods, so
                // this filter never touches the built-ins.
                if (attr && attr != PROTO_NONE) {
                    // Symbols are interned per-ProtoSpace: a function-local
                    // `static` would bind to the FIRST runtime's space and
                    // dangle for every later STRuntime (the multi-runtime unit
                    // harness). Resolve fresh from the live ctx each send —
                    // the same discipline exception_prims.cpp uses.
                    const proto::ProtoString* classSideKey =
                        proto::ProtoString::createSymbol(ctx, "__class_side__");
                    const proto::ProtoString* classNameKey =
                        proto::ProtoString::createSymbol(ctx, "__class_name__");
                    const proto::ProtoObject* csMark =
                        attr->getAttribute(ctx, classSideKey);
                    if (csMark == PROTO_TRUE) {
                        const proto::ProtoObject* ownName =
                            recv->getOwnAttributeDirect(ctx, classNameKey);
                        bool recvIsClass = ownName && ownName != PROTO_NONE;
                        if (!recvIsClass)
                            attr = nullptr;   // hide it — instance receiver
                    }
                }
                if (!attr || attr == PROTO_NONE) {
                    // D3 (MNT-b2): an unresolved selector is NOT a hard abort.
                    // It happens inside the engine's own SEND dispatch, never
                    // inside a primitive, so it bypasses the EXC-d
                    // `translateNativeException` boundary (which wraps only the
                    // primitive call below). Signal a catchable
                    // `MessageNotUnderstood` (a subclass of `Error`) through
                    // the normal `signalInstance` handler-stack path, so
                    // `[ obj bogusSel ] on: Error do: [:e| …]` catches it and
                    // `e messageText` reads back informatively. With no
                    // handler, `defaultAction` throws `UnhandledSTException`,
                    // preserving the previous uncaught-error behaviour for the
                    // top level / REPL. `UnwindToHandler` from a `return:`
                    // handler propagates out of runLoop untouched (the engine
                    // does not catch it) straight to the owning `on:do:`.
                    std::string mntMsg = "doesNotUnderstand: " + selStr;
                    auto* r = signalErrorOfClass(
                        rt_, ctx, rt_.bootstrap().messageNotUnderstoodProto,
                        mntMsg.c_str());
                    // A resumable handler (`resume:`) would let `signalInstance`
                    // return a value here — push it as the send's result. A
                    // MessageNotUnderstood is non-resumable, so in practice the
                    // line above either threw or this pushes nil; handling it
                    // keeps the send well-formed regardless.
                    push(f, r ? r : PROTO_NONE);
                    break;
                }
                // F4-U4: detect user method (Block-shaped wrapper carrying
                // __bc_ptr__). User methods are installed by Compiler-emitted
                // __installMethod:as: and are not tagged primitive markers.
                // We probe for __bc_ptr__ first; if absent, fall through to
                // the legacy primitive-marker (tagged SmallInteger with bit
                // 62 set) dispatch.
                const proto::ProtoString* bcKey =
                    rt_.bootstrap().sym.bcPtr;
                const proto::ProtoString* capKey =
                    rt_.bootstrap().sym.captured;
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
                //
                // EXC-d: this is the engine's single native-primitive call
                // boundary. Wrap it in translateNativeException so a C++
                // exception thrown by the primitive (a std::runtime_error
                // from a bad selector / arity / arithmetic, or any UMD-native
                // throw) becomes a catchable protoST Error. The control-flow
                // siblings (NonLocalReturn / UnwindToHandler / RetrySignal /
                // ResumeSignal / PassSignal / FutureYield) and DebuggerHalt /
                // UnhandledSTException are re-thrown untouched by the wrapper,
                // so primitives that raise them on purpose are unaffected.
                auto* result = translateNativeException(
                    rt_, ctx,
                    [&] { return fn(rt_, ctx, recv, sendArgs, argcOp); });
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
                auto* sym = f.m->constSym(ctx, arg);
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
                auto* sym = f.m->constSym(ctx, arg);
                // `sym` is an interned (perennial) ProtoString held across
                // setAttribute on the mutable captured dict, which allocates a
                // sparse-list node. `val` was popped but stays rooted in its
                // frame slot; `sym` is reachable from nowhere — pin it.
                TransientPin pinSym(
                    ctx, reinterpret_cast<const proto::ProtoObject*>(sym));
                const_cast<proto::ProtoObject*>(capD)->setAttribute(ctx, sym, val);
                break;
            }
            case Op::PUSH_GLOBAL: {
                // arg = constant pool index of the (interned) global name.
                const std::string& nameStr = f.m->constSymbol(arg);
                auto* sym = f.m->constSym(ctx, arg);
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
                auto* sym = f.m->constSym(ctx, arg);
                // `sym` is interned (perennial); held across setAttribute on
                // the mutable globals object (allocates) — pin it.
                TransientPin pinSym(
                    ctx, reinterpret_cast<const proto::ProtoObject*>(sym));
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
                // Mangled "_iv_<name>" key, interned once and cached on the
                // module (BytecodeModule::ivSymbol) — NOT rebuilt as a
                // std::string and re-interned on every instance-variable
                // access, which the profile measured as the dominant cost.
                auto* sym = f.m->ivSymbol(ctx, arg);
                // CLO Part 1: read `self` from the frame's header self-slot
                // (baseSlot+1). For a method frame this equals local 0; for a
                // block frame local 0 is the first block argument, so the
                // header slot is the only correct source. PUSH_BLOCK stamps
                // the block's self and block invocation primes that slot.
                const proto::ProtoObject* self = getSelf(f);
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
                // Mangled "_iv_<name>" key, interned once and cached on the
                // module (BytecodeModule::ivSymbol) — NOT rebuilt as a
                // std::string and re-interned on every instance-variable
                // access, which the profile measured as the dominant cost.
                auto* sym = f.m->ivSymbol(ctx, arg);
                // F6 v3 E5: `sym` held across setAttribute on `self` (a
                // mutable object — allocates a sparse-list node). `val` and
                // `self` stay rooted via their frame slots; `sym` does not.
                TransientPin pinSym(
                    ctx, reinterpret_cast<const proto::ProtoObject*>(sym));
                // CLO Part 1: write to `self` from the frame's header
                // self-slot (baseSlot+1) — correct for both method and block
                // frames (see PUSH_INSTVAR rationale).
                const proto::ProtoObject* self = getSelf(f);
                if (!self || self == PROTO_NONE)
                    throw std::runtime_error("STORE_INSTVAR self is null");
                const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, sym, val);
                break;
            }
            case Op::MAKE_CAPTURED: {
                // CLO Part 2: allocate a fresh per-method (or per-block)
                // captured dict and install it in frame slot 0, where
                // getCaptured / PUSH_CAPTURED / STORE_CAPTURED read it. The
                // compiler emits this in the prologue of a method whose
                // captured set is non-empty, before any STORE_CAPTURED. A
                // nested block does NOT emit it — PUSH_BLOCK already stamped
                // the block with the creating frame's captured dict.
                auto* dict = const_cast<proto::ProtoObject*>(
                                 rt_.bootstrap().objectProto)
                                 ->newChild(ctx, /*isMutable=*/true);
                // `dict` is a fresh mutable object reachable from nowhere the
                // GC traces until setCaptured lands it in the frame's slot 0.
                // setAutomaticLocal does not allocate, so a TransientPin is
                // strictly needed only if anything allocated between newChild
                // and the store — nothing does. Pin defensively anyway.
                TransientPin pinDict(ctx, dict);
                setCaptured(f, dict);
                break;
            }
            case Op::MAKE_ARRAY: {
                // COL-a: collection-literal builder. `arg` element values are
                // on the operand stack, oldest-pushed deepest (so element 0 is
                // at depth arg-1). Collect them bottom-up into a ProtoList,
                // pop them, wrap the list as a fresh Array instance and push.
                if (f.sp < arg)
                    throw std::runtime_error("MAKE_ARRAY with insufficient stack");
                const proto::ProtoList* data = ctx->newList();
                // `data` is a transient reachable from nowhere the GC traces;
                // it is rebuilt by appendLast each turn (structural-sharing
                // COW). The element values stay rooted via their frame slots
                // (pop only decrements sp; the slots remain GC-traced). A
                // single pin tracks the latest list value.
                TransientPin pinData(
                    ctx, reinterpret_cast<const proto::ProtoObject*>(data));
                for (unsigned int i = 0; i < arg; ++i) {
                    const proto::ProtoObject* v = opAt(f, arg - 1 - i);
                    data = data->appendLast(ctx, v ? v : PROTO_NONE);
                    pinData.reset(
                        reinterpret_cast<const proto::ProtoObject*>(data));
                }
                for (unsigned int i = 0; i < arg; ++i) pop(f);
                const proto::ProtoObject* arr =
                    makeArrayInstance(rt_, ctx, data);
                push(f, arr ? arr : PROTO_NONE);
                break;
            }
            case Op::PUSH_SELF: {
                // CLO Part 1: push the current frame's `self` from the header
                // self-slot (getSelf, baseSlot+1). pushFrame primes that slot
                // for method frames (= the receiver) and block invocation
                // primes it from the block's stamped `__block_self__`, so it
                // is correct for both. Reading local 0 (the old behaviour)
                // would be wrong for a block whose local 0 is its first arg.
                const proto::ProtoObject* self = getSelf(f);
                if (!self) self = PROTO_NONE;
                push(f, self);
                break;
            }
            case Op::PUSH_SUPER: {
                // BL-1: a `super` send. `super` evaluates to the SAME object
                // as `self`; what differs is method lookup, which must start
                // one level above the class defining the current method.
                // We push `self` (so the receiver is correct) and arm
                // f.superPending so the next SEND_* redirects its lookup.
                // CLO Part 1: read from the header self-slot (see PUSH_SELF).
                const proto::ProtoObject* self = getSelf(f);
                if (!self) self = PROTO_NONE;
                push(f, self);
                f.superPending = true;
                break;
            }
            case Op::DUP_RECEIVER: {
                // BL-1: duplicate the operand-stack value at depth `arg`
                // (0 == top) and push the copy. Used to keep a receiver
                // around for a follow-up send (cascades, multi-send
                // patterns). The current compiler emits plain DUP for
                // cascades, so this is reached only by hand-written or
                // future bytecode; implement it faithfully regardless.
                if (f.sp <= arg)
                    throw std::runtime_error("DUP_RECEIVER depth out of range");
                push(f, opAt(f, arg));
                break;
            }
            case Op::HALT:
                // BL-1: clean program-end terminator. Treated like an
                // implicit RETURN_TOP: hand the current top-of-stack (or
                // PROTO_NONE) back to the caller frame, or out to the C++
                // caller when this is the last frame.
                {
                    const proto::ProtoObject* r =
                        f.sp == 0 ? PROTO_NONE : peek(f);
                    popFrame();
                    if (frames_.empty()) return r;
                    push(frames_.back(), r);
                    continue;
                }
            case Op::EXTEND:
                // BL-2: unreachable — the decode loop above consumes every
                // EXTEND prefix before the switch. A bare EXTEND reaching
                // here would mean a malformed instruction stream.
                throw std::runtime_error(
                    "ExecutionEngine: stray EXTEND prefix at pc=" +
                    std::to_string(f.pc - kInstrSize));
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
    } catch (const NonLocalReturn& nlr) {
        // Track 1 slice 1: a non-local return is propagating up the C++
        // stack. It was thrown either by THIS engine's RETURN handler (home
        // not in our frames_) or by a nested engine reached via invokeBlock.
        // Either way, check whether the home method activation lives in this
        // engine's frame stack.
        std::size_t homeIdx = frames_.size();
        for (std::size_t i = frames_.size(); i-- > 0; ) {
            if (frames_[i].frameId == nlr.homeFrameId()) {
                homeIdx = i;
                break;
            }
        }
        if (homeIdx == frames_.size()) {
            // The home belongs to an outer engine — let the exception
            // propagate. invokeBlock's nested-engine call site does NOT
            // swallow it; it bubbles to the parent engine's runLoop, which
            // repeats this check.
            throw;
        }
        // The home is ours: unwind every frame from the top down to and
        // including the home frame, then resume normally with the value
        // pushed onto the home frame's caller (or return it to the C++
        // caller if the home was the outermost frame here).
        const proto::ProtoObject* r = nlr.value();
        while (frames_.size() > homeIdx)
            popFrame();
        if (frames_.empty())
            return r ? r : PROTO_NONE;
        push(frames_.back(), r ? r : PROTO_NONE);
        // Fall through to the outer while(true) so the dispatch loop
        // resumes on the (now top) home-caller frame.
        continue;
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
            rt_.bootstrap().sym.suspendedFrame;
        const proto::ProtoString* waitingOnKey =
            rt_.bootstrap().sym.waitingOn;
        // F6 v3 E5: `suspKey` / `waitingOnKey` are freshly interned strings
        // held across snapshotFrames (which allocates a ProtoList plus one
        // mutable object and several lists per frame — heavy GC pressure) and
        // the setAttribute calls below. Pin both.
        TransientPin pinSuspKey(
            ctx, reinterpret_cast<const proto::ProtoObject*>(suspKey));
        TransientPin pinWaitKey(
            ctx, reinterpret_cast<const proto::ProtoObject*>(waitingOnKey));

        const proto::ProtoObject* snap = snapshotFrames(ctx);
        // `snap` is held across the setAttribute below and across
        // appendFutureWaiter (which allocates) — pin it.
        TransientPin pinSnap(ctx, snap);
        SCHED_DIAG("engine YIELD actor=" << actor
                   << " future=" << y.future()
                   << " frames=" << frames_.size());
        const_cast<proto::ProtoObject*>(actor)->setAttribute(ctx, suspKey, snap);
        if (y.future()) {
            const_cast<proto::ProtoObject*>(actor)->setAttribute(
                ctx, waitingOnKey, y.future());

            // CAS-append the actor to the future's __waiters__ list. The
            // helper returns false if the future had ALREADY settled — its
            // settle drained __waiters__ before our append landed — in which
            // case we must schedule the actor ourselves so the resume path
            // runs and consumes the settled value.
            //
            // We import the helper indirectly from future_prims.cpp; the
            // linker connects them.
            extern bool appendFutureWaiter(
                proto::ProtoContext* ctx,
                const proto::ProtoObject* fut,
                const proto::ProtoObject* waiterActor);
            bool parked = appendFutureWaiter(ctx, y.future(), actor);
            if (!parked) {
                // The future settled before our append landed; the settle's
                // waiter drain did not see this actor. Schedule it explicitly
                // so the resume path
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
            dframe = makeDebugStack();
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
    const proto::ProtoString* pcKey       = proto::ProtoString::createSymbol(ctx, "pc");
    const proto::ProtoString* opStackKey  = proto::ProtoString::createSymbol(ctx, "op_stack");
    const proto::ProtoString* localsKey   = proto::ProtoString::createSymbol(ctx, "locals");
    const proto::ProtoString* selfKey     = proto::ProtoString::createSymbol(ctx, "self_obj");
    const proto::ProtoString* capturedKey = proto::ProtoString::createSymbol(ctx, "captured_dict");
    const proto::ProtoString* mPtrKey     = proto::ProtoString::createSymbol(ctx, "m_ptr");
    // Track 1 slice 1: frame identity. Global ids stay valid across a
    // yield/resume with no renumbering, so a non-local return that survives a
    // cooperative yield still finds its home after restore.
    const proto::ProtoString* frameIdKey  = proto::ProtoString::createSymbol(ctx, "frame_id");
    const proto::ProtoString* homeIdKey   = proto::ProtoString::createSymbol(ctx, "home_frame_id");
    // F6 v3 E5: the attribute keys are freshly interned ProtoStrings held
    // in C++ locals for the entire frame-encoding loop, which allocates one
    // mutable object plus three lists plus an ExternalPointer PER FRAME — a
    // deep cooperative chain snapshots dozens of frames, so a GC cycle mid-
    // loop is near-certain under aggressive GC. Pin every key for the loop.
    TransientPin pinPcKey(ctx, reinterpret_cast<const proto::ProtoObject*>(pcKey));
    TransientPin pinOpKey(ctx, reinterpret_cast<const proto::ProtoObject*>(opStackKey));
    TransientPin pinLocKey(ctx, reinterpret_cast<const proto::ProtoObject*>(localsKey));
    TransientPin pinSelfKey(ctx, reinterpret_cast<const proto::ProtoObject*>(selfKey));
    TransientPin pinCapKey(ctx, reinterpret_cast<const proto::ProtoObject*>(capturedKey));
    TransientPin pinMKey(ctx, reinterpret_cast<const proto::ProtoObject*>(mPtrKey));
    TransientPin pinFidKey(ctx, reinterpret_cast<const proto::ProtoObject*>(frameIdKey));
    TransientPin pinHidKey(ctx, reinterpret_cast<const proto::ProtoObject*>(homeIdKey));

    // `result` accumulates one frameObj per frame via appendLast (COW); a
    // single pin tracks the latest list value.
    const proto::ProtoList* result = ctx->newList();
    TransientPin pinResult(
        ctx, reinterpret_cast<const proto::ProtoObject*>(result));
    for (const Frame& fr : frames_) {
        // Each frame becomes a fresh mutable child of objectProto. This
        // mirrors how Object>>asActor builds the actor wrapper and how the
        // SEND fast-path builds message envelopes — see the
        // `bootstrap.objectProto->newChild(ctx, /*isMutable=*/true)` pattern
        // elsewhere in this file.
        auto* frameObj = const_cast<proto::ProtoObject*>(rt_.bootstrap().objectProto)
            ->newChild(ctx, /*isMutable=*/true);
        // F6 v3 E5: `frameObj` is held across every setAttribute, the two
        // inner appendLast loops, fromLong / fromExternalPointer, and the
        // final result->appendLast. Pin it for the iteration.
        TransientPin pinFrameObj(ctx, frameObj);

        // pc
        frameObj->setAttribute(
            ctx, pcKey,
            ctx->fromLong(static_cast<long long>(fr.pc)));

        // op_stack as ProtoList (preserve order: index 0 = bottom of stack).
        // F6 v3 E3: the live operand stack is the [0, sp) slice of the
        // frame's operand-stack slot region inside automaticLocals.
        // F6 v3 E5: `opList` is rebuilt by appendLast each turn; a single pin
        // tracks the latest value across the inner loop's allocations.
        const proto::ProtoList* opList = ctx->newList();
        TransientPin pinOpList(
            ctx, reinterpret_cast<const proto::ProtoObject*>(opList));
        for (unsigned int j = 0; j < fr.sp; ++j) {
            const proto::ProtoObject* v =
                ctx->getAutomaticLocal(opStackBase(fr) + j);
            opList = opList->appendLast(ctx, v ? v : PROTO_NONE);
            pinOpList.reset(
                reinterpret_cast<const proto::ProtoObject*>(opList));
        }
        frameObj->setAttribute(ctx, opStackKey, opList->asObject(ctx));

        // locals as ProtoList (size == localCount). F6 v3 E3: read every
        // local slot out of the frame's region.
        // F6 v3 E5: same per-turn rebuild as opList — single pin via reset.
        const proto::ProtoList* locList = ctx->newList();
        TransientPin pinLocList(
            ctx, reinterpret_cast<const proto::ProtoObject*>(locList));
        for (unsigned int j = 0; j < fr.localCount; ++j) {
            const proto::ProtoObject* v = getLocal(fr, j);
            locList = locList->appendLast(ctx, v ? v : PROTO_NONE);
            pinLocList.reset(
                reinterpret_cast<const proto::ProtoObject*>(locList));
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

        // Track 1 slice 1: frame identity. Stored as SmallIntegers; the
        // global ids are NOT renumbered on restore so a suspended home frame
        // resolves the same way after resume.
        frameObj->setAttribute(
            ctx, frameIdKey,
            ctx->fromLong(static_cast<long long>(fr.frameId)));
        frameObj->setAttribute(
            ctx, homeIdKey,
            ctx->fromLong(static_cast<long long>(fr.homeFrameId)));

        result = result->appendLast(ctx, frameObj);
        pinResult.reset(
            reinterpret_cast<const proto::ProtoObject*>(result));
    }
    return result->asObject(ctx);
}

void
ExecutionEngine::restoreFrames(proto::ProtoContext* ctx,
                               const proto::ProtoObject* snapshot) {
    // F6 v3 E3: restoreFrames is the engine's entry point on the resume path
    // (drainOne calls it before resumeWith + continueRun). Establish the same
    // engine-context state runWithArgs would: bind ctx_, ensure the GC-traced
    // slot array is pre-sized, and anchor this engine's frame regions at the
    // current thread-local cursor so a nested engine cannot overlap them.
    //
    // F6 v3 E5: the resize MUST happen before any TransientPin is claimed —
    // the pin writes into the scratch region of automaticLocals, which only
    // exists once the array is sized to kSlotCapacity. The worker context on
    // a resume may be a fresh per-thread context never previously sized by a
    // runWithArgs, so this resize is load-bearing for the pins below.
    ctx_ = ctx;
    ctx_->resizeAutomaticLocals(kSlotCapacity);

    // Resolve fresh from the live ctx — see snapshotFrames for the
    // per-space interning rationale.
    const proto::ProtoString* pcKey       = proto::ProtoString::createSymbol(ctx, "pc");
    const proto::ProtoString* opStackKey  = proto::ProtoString::createSymbol(ctx, "op_stack");
    const proto::ProtoString* localsKey   = proto::ProtoString::createSymbol(ctx, "locals");
    const proto::ProtoString* selfKey     = proto::ProtoString::createSymbol(ctx, "self_obj");
    const proto::ProtoString* capturedKey = proto::ProtoString::createSymbol(ctx, "captured_dict");
    const proto::ProtoString* mPtrKey     = proto::ProtoString::createSymbol(ctx, "m_ptr");
    // Track 1 slice 1: frame identity (see snapshotFrames).
    const proto::ProtoString* frameIdKey  = proto::ProtoString::createSymbol(ctx, "frame_id");
    const proto::ProtoString* homeIdKey   = proto::ProtoString::createSymbol(ctx, "home_frame_id");
    // F6 v3 E5: the keys are held across the per-frame getAttribute loop
    // (each turn interns nothing, but a getAttribute walk that crosses a
    // safepoint plus the array writes below can sit either side of a GC
    // cycle). Pin them — the context was sized above so the scratch region
    // exists.
    TransientPin pinPcKey(ctx, reinterpret_cast<const proto::ProtoObject*>(pcKey));
    TransientPin pinOpKey(ctx, reinterpret_cast<const proto::ProtoObject*>(opStackKey));
    TransientPin pinLocKey(ctx, reinterpret_cast<const proto::ProtoObject*>(localsKey));
    TransientPin pinSelfKey(ctx, reinterpret_cast<const proto::ProtoObject*>(selfKey));
    TransientPin pinCapKey(ctx, reinterpret_cast<const proto::ProtoObject*>(capturedKey));
    TransientPin pinMKey(ctx, reinterpret_cast<const proto::ProtoObject*>(mPtrKey));
    TransientPin pinFidKey(ctx, reinterpret_cast<const proto::ProtoObject*>(frameIdKey));
    TransientPin pinHidKey(ctx, reinterpret_cast<const proto::ProtoObject*>(homeIdKey));

    if (!snapshot)
        throw std::runtime_error("restoreFrames: snapshot is null");
    auto* asList = snapshot->asList(ctx);
    if (!asList)
        throw std::runtime_error("restoreFrames: snapshot is not a list");

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

        // Track 1 slice 1: frame identity. Older snapshots (none exist on
        // disk, but be defensive) without these keys fall back to 0 — the
        // frame then becomes its own home, i.e. plain local returns.
        unsigned long frameId = 0;
        unsigned long homeId  = 0;
        auto* frameIdVal = frameObj->getAttribute(ctx, frameIdKey);
        if (frameIdVal && frameIdVal != PROTO_NONE)
            frameId = static_cast<unsigned long>(frameIdVal->asLong(ctx));
        auto* homeIdVal = frameObj->getAttribute(ctx, homeIdKey);
        if (homeIdVal && homeIdVal != PROTO_NONE)
            homeId = static_cast<unsigned long>(homeIdVal->asLong(ctx));

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
        // Track 1 slice 1: restore the global ids verbatim — no renumbering,
        // so a non-local return that survives the yield finds its home.
        fr.frameId    = frameId;
        fr.homeFrameId = (homeId == 0) ? frameId : homeId;

        const unsigned int regionSize = frameRegionSize(fr);
        const unsigned int regionEnd  = fr.baseSlot + regionSize;
        // F6 v3 E5: keep frame regions out of the transient-pin scratch region.
        if (regionEnd > kFrameRegionLimit)
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
