#include "DebuggerRuntime.h"
#include "runtime/UnhandledSTException.h"
#include "protoST/STRuntime.h"
#include "../runtime/BytecodeModule.h"
#include "../runtime/Bootstrap.h"
#include "../runtime/ExecutionEngine.h"
#include "../runtime/HandlerStack.h"
#include "../runtime/ValueFormat.h"
#include "../runtime/Opcodes.h"
#include "../frontend/Parser.h"
#include "../frontend/Compiler.h"
#include "protoCore.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cctype>
#include <unordered_set>

namespace protoST {

// invokeBlock -- defined in block_prims.cpp.
const proto::ProtoObject* invokeBlock(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* block,
                                      const proto::ProtoObject* const* args, int argc);

// One thread's debugger state. Created the first time the thread meets the
// debugger, owned by that thread (a thread_local list frees it at the
// thread's exit), registered -- given an id and announced to the frontend --
// the first time it runs script code while a debugger is attached.
struct DebugThreadState {
    // The debugger this state belongs to. Written under ownersMu(): by the
    // thread's exit (cleared) and by the debugger's destructor (orphaned).
    DebuggerRuntime*     owner = nullptr;
    int                  id = 0;            // 0 until registered
    bool                 registered = false;
    std::string          baseName;
    // Stepping (this thread only).
    std::atomic<uint8_t> mode{0};
    int                  stepDepth = 0;
    const BytecodeModule* stepModule = nullptr;
    int                  stepLine = 0;
    std::atomic<bool>    pauseRequested{false};
    int                  suppress = 0;      // > 0: no stops (SuppressStops)
};

namespace {
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// Guards DebugThreadState::owner across a thread's exit and the debugger's
// destruction. Never destroyed: a thread may exit after static destruction.
std::mutex& ownersMu() {
    static std::mutex* m = new std::mutex;
    return *m;
}

thread_local std::string t_baseName;

// The calling thread's debugger states, one per debugger it met (in practice
// one). At thread exit each is reported to its debugger, if still alive.
struct ThreadStates {
    std::vector<DebugThreadState*> states;
    DebugThreadState* last = nullptr;
    ~ThreadStates() {
        for (DebugThreadState* ts : states) {
            {
                std::lock_guard<std::mutex> lk(ownersMu());
                if (ts->owner) ts->owner->threadExited(ts);
                ts->owner = nullptr;
            }
            delete ts;
        }
    }
};
thread_local ThreadStates t_states;

bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    static const std::unordered_set<std::string> reserved = {
        "self", "super", "nil", "true", "false", "thisContext"};
    return reserved.count(s) == 0;
}

// Parse and compile `src` as a module; nullptr (and `err`) on failure.
std::unique_ptr<BytecodeModule> compileSource(const std::string& src, std::string& err) {
    protoST::Parser pp(src);
    auto ast = pp.parseModule();
    if (!pp.errors().empty()) {
        err = "parse error: " + pp.errors().front().message;
        return nullptr;
    }
    protoST::Compiler cc;
    auto bc = cc.compileModule(*ast);
    if (cc.hasErrors()) {
        err = "compile error: " + cc.errors().front();
        return nullptr;
    }
    return bc;
}
} // anon

DebuggerRuntime::~DebuggerRuntime() {
    // A registered thread may outlive this debugger (the main thread of a
    // `-d` session, a thread of another runtime): orphan its state.
    std::lock_guard<std::mutex> lk(ownersMu());
    std::lock_guard<std::mutex> tl(threadsMu_);
    for (auto& [id, ts] : threads_) { (void)id; ts->owner = nullptr; }
    threads_.clear();
}

void DebuggerRuntime::setFrontend(DebuggerFrontend* f) {
    // Under ownersMu: a thread's exit reports to the frontend under it, so
    // once this returns with nullptr no exit event reaches the old frontend.
    std::lock_guard<std::mutex> lk(ownersMu());
    frontend_.store(f);
}

DebugThreadState* DebuggerRuntime::currentThread(proto::ProtoContext* ctx) {
    ThreadStates& tsl = t_states;
    if (tsl.last && tsl.last->owner == this) return tsl.last;
    DebugThreadState* ts = nullptr;
    for (DebugThreadState* s : tsl.states)
        if (s->owner == this) { ts = s; break; }
    if (!ts) {
        ts = new DebugThreadState;
        ts->baseName = t_baseName;
        {
            std::lock_guard<std::mutex> lk(ownersMu());
            ts->owner = this;
        }
        tsl.states.push_back(ts);
    }
    tsl.last = ts;
    (void)ctx;
    return ts;
}

DebugThreadState* DebuggerRuntime::registerThread(DebugThreadState* ts, proto::ProtoContext* ctx) {
    if (ts->registered) return ts;
    {
        std::lock_guard<std::mutex> lk(threadsMu_);
        ts->id = nextThreadId_++;
        if (ts->baseName.empty()) ts->baseName = "thread " + std::to_string(ts->id);
        ts->registered = true;
        threads_[ts->id] = ts;
    }
    if (DebuggerFrontend* fe = frontend_.load()) {
        // The event is written to the client, which can block on a full pipe.
        if (ctx) {
            proto::ProtoContext::UnmanagedScope u(ctx);
            fe->onThreadEvent(ts->id, ts->baseName, /*started=*/true);
        } else {
            fe->onThreadEvent(ts->id, ts->baseName, /*started=*/true);
        }
    }
    return ts;
}

void DebuggerRuntime::threadExited(DebugThreadState* ts) {
    // Called under ownersMu() with ts->owner == this.
    bool wasRegistered;
    {
        std::lock_guard<std::mutex> lk(threadsMu_);
        wasRegistered = ts->registered;
        threads_.erase(ts->id);
    }
    if (wasRegistered)
        if (DebuggerFrontend* fe = frontend_.load())
            fe->onThreadEvent(ts->id, ts->baseName, /*started=*/false);
}

int DebuggerRuntime::nameCurrentThread(const std::string& name, proto::ProtoContext* ctx) {
    t_baseName = name;
    DebugThreadState* ts = currentThread(ctx);
    if (!ts->registered) {
        ts->baseName = name;
        registerThread(ts, ctx);
    } else {
        std::lock_guard<std::mutex> lk(threadsMu_);
        ts->baseName = name;
    }
    return ts->id;
}

void DebuggerRuntime::setThreadBaseName(const std::string& name) { t_baseName = name; }

bool DebuggerRuntime::requestPause(int threadId) {
    std::lock_guard<std::mutex> lk(threadsMu_);
    auto it = threads_.find(threadId);
    if (it == threads_.end()) return false;
    it->second->pauseRequested.store(true, std::memory_order_relaxed);
    return true;
}

DebuggerRuntime::SuppressStops::SuppressStops(DebuggerRuntime& d)
    : ts_(d.currentThread(nullptr)) { ++ts_->suppress; }
DebuggerRuntime::SuppressStops::~SuppressStops() { --ts_->suppress; }

void DebuggerRuntime::setMode(Mode m) {
    currentThread(nullptr)->mode.store(static_cast<uint8_t>(m), std::memory_order_relaxed);
}
DebuggerRuntime::Mode DebuggerRuntime::mode() const {
    auto* self = const_cast<DebuggerRuntime*>(this);
    return static_cast<Mode>(self->currentThread(nullptr)->mode.load(std::memory_order_relaxed));
}

bool DebuggerRuntime::stepShouldStop(DebugThreadState& ts, const BytecodeModule* m, size_t pc,
                                     int depth) {
    const Mode md = static_cast<Mode>(ts.mode.load(std::memory_order_relaxed));
    if (depth < ts.stepDepth) return true;              // the frame returned
    if (md == Mode::StepOut) return false;
    if (md == Mode::StepOver && depth > ts.stepDepth) return false;   // inside a call
    const int line = m ? m->lineForPc(pc) : 0;
    if (line <= 0) return false;
    if (depth > ts.stepDepth) return true;              // step in: a new frame
    return m != ts.stepModule || line != ts.stepLine;   // the next line
}

void DebuggerRuntime::emitOutput(proto::ProtoContext* ctx, const std::string& category,
                                 const std::string& text) {
    DebuggerFrontend* fe = frontend_.load();
    if (!fe) return;
    // Written to the client, which can block on a full pipe.
    proto::ProtoContext::UnmanagedScope u(ctx);
    fe->onOutput(category, text);
}

void DebuggerRuntime::onInstruction(STRuntime& rt, proto::ProtoContext* ctx,
                                    const BytecodeModule* m, size_t pc) {
    DebugThreadState* ts = currentThread(ctx);
    if (ts->suppress) return;
    if (!ts->registered) registerThread(ts, ctx);
    const std::shared_ptr<BreakpointSpec> spec = breakpoints_.find(m, pc);
    const bool pause = ts->pauseRequested.load(std::memory_order_relaxed);
    const Mode md = static_cast<Mode>(ts->mode.load(std::memory_order_relaxed));
    if (!spec && !pause && md == Mode::Free) return;

    // A breakpoint with a condition, a hit condition or a log message decides
    // here, on this thread, with stops suppressed while its expressions run.
    bool bp = false;
    std::string bpError;
    if (spec) {
        if (spec->plain()) {
            bp = true;
        } else {
            const DebugFrame snap = ExecutionEngine::snapshotThreadStack(rt, /*innermostAtInstruction=*/true);
            bool pass = true;
            if (!spec->condition.empty())
                pass = evaluateCondition(rt, ctx, spec->condition, snap, {}, bpError);
            if (!bpError.empty()) {
                bpError = "breakpoint " + bpError;
                emitOutput(ctx, "stderr", bpError + "\n");
                bp = true;                       // a failing condition stops
            } else if (pass && spec->countHit()) {
                if (spec->isLogpoint)
                    emitOutput(ctx, "console", interpolateLogMessage(rt, ctx, spec->logMessage, snap) + "\n");
                else
                    bp = true;
            }
        }
    }
    if (!bp && !pause && md == Mode::Free) return;

    const char* reason;
    if (bp) {
        reason = "breakpoint";
    } else if (pause) {
        reason = "pause";
    } else if (md == Mode::SingleStep || md == Mode::RunToReturn) {
        reason = "step";
    } else {
        if (!stepShouldStop(*ts, m, pc, ExecutionEngine::threadFrameDepth(rt))) return;
        reason = "step";
    }
    ts->pauseRequested.store(false, std::memory_order_relaxed);
    StopInfo si;
    si.reason      = reason;
    si.description = bpError;
    enterSession(rt, ctx, ExecutionEngine::snapshotThreadStack(rt, /*innermostAtInstruction=*/true),
                 reason, &si);
}

void DebuggerRuntime::setExceptionConditions(const std::string& raised, const std::string& uncaught) {
    std::lock_guard<std::mutex> lk(conditionsMu_);
    raisedCondition_   = raised;
    uncaughtCondition_ = uncaught;
}

void DebuggerRuntime::onException(STRuntime& rt, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* exc,
                                  const std::string& className, const std::string& message,
                                  bool uncaught, bool fromPrimitive) {
    DebugThreadState* ts = currentThread(ctx);
    if (ts->suppress) return;
    const unsigned f = exceptionFilters();
    bool byRaised   = (f & kFilterRaised) != 0;
    bool byUncaught = (f & kFilterUncaught) && uncaught;
    if (!byRaised && !byUncaught) return;
    if (!ts->registered) registerThread(ts, ctx);

    // The filters' conditions, evaluated in the signalling frame with the
    // exception bound as `ex`. A condition that fails to evaluate stops.
    std::string raisedCond, uncaughtCond;
    {
        std::lock_guard<std::mutex> lk(conditionsMu_);
        raisedCond   = raisedCondition_;
        uncaughtCond = uncaughtCondition_;
    }
    DebugFrame snap = ExecutionEngine::snapshotThreadStack(rt, /*innermostAtInstruction=*/false);
    std::string condError;
    auto passes = [&](const std::string& cond) {
        if (cond.empty()) return true;
        std::string err;
        const bool r = evaluateCondition(rt, ctx, cond, snap, {FrameVariable{"ex", exc}}, err);
        if (!err.empty() && condError.empty()) condError = "exception breakpoint " + err;
        return r;
    };
    if (byRaised)   byRaised   = passes(raisedCond);
    if (byUncaught) byUncaught = passes(uncaughtCond);
    if (!byRaised && !byUncaught) return;
    if (!condError.empty()) emitOutput(ctx, "stderr", condError + "\n");

    StopInfo si;
    si.reason           = "exception";
    si.isException      = true;
    si.exceptionClass   = className.empty() ? std::string("Exception") : className;
    si.exceptionMessage = message;
    si.breakMode        = byUncaught ? "unhandled" : "always";
    si.description      = si.exceptionClass;
    if (!message.empty() && message != si.exceptionClass) si.description += ": " + message;
    if (byUncaught) si.description += " (no handler will catch it)";
    if (fromPrimitive) si.description += " -- raised by a primitive; stopped at the calling frame";
    if (!condError.empty()) si.description += " [" + condError + "]";
    si.stackText = ExecutionEngine::describeActiveStack(20);
    // The conditions ran code; snapshot again.
    enterSession(rt, ctx, ExecutionEngine::snapshotThreadStack(rt, /*innermostAtInstruction=*/false),
                 "exception", &si);
}

std::string DebuggerRuntime::threadDisplayName(STRuntime& rt, proto::ProtoContext* ctx,
                                               const DebugThreadState& ts) {
    std::string name;
    {
        std::lock_guard<std::mutex> lk(threadsMu_);
        name = ts.baseName;
    }
    const proto::ProtoObject* actor = rt.currentActor();
    if (!actor || actor == PROTO_NONE || !ctx) return name;
    const proto::ProtoObject* target = actor->getAttribute(ctx, rt.bootstrap().sym.wrapped);
    std::string cls = protoST::formatValue(rt, ctx, (target && target != PROTO_NONE) ? target : actor);
    if (cls.rfind("an ", 0) == 0) cls = cls.substr(3);
    else if (cls.rfind("a ", 0) == 0) cls = cls.substr(2);
    int n;
    {
        std::lock_guard<std::mutex> lk(threadsMu_);
        auto [it, inserted] = actorIds_.try_emplace(actor, nextActorId_);
        if (inserted) ++nextActorId_;
        n = it->second;
    }
    return name + ": actor " + cls + "#" + std::to_string(n);
}

void DebuggerRuntime::applyCommand(DebugThreadState& ts, Command cmd, const DebugFrame& frame) {
    ts.stepDepth  = static_cast<int>(frame.callStack.size());
    ts.stepModule = frame.module;
    ts.stepLine   = frame.line;
    Mode m = Mode::Free;
    switch (cmd) {
        case Command::Continue: m = Mode::Free;     break;
        case Command::Step:     m = Mode::StepIn;   break;
        // Before the first instruction (the entry stop) there is no frame to
        // step over: the first line is the next stop either way.
        case Command::Next:     m = frame.callStack.empty() ? Mode::StepIn : Mode::StepOver; break;
        case Command::Finish:   m = Mode::StepOut;  break;
    }
    ts.mode.store(static_cast<uint8_t>(m), std::memory_order_relaxed);
}

std::vector<FrameVariable> DebuggerRuntime::frameVariables(STRuntime& rt, proto::ProtoContext* ctx,
                                                           const DebugFrame& f) {
    std::vector<FrameVariable> out;
    const BytecodeModule* m = f.module;
    if (!m) return out;
    const proto::ProtoObject* objectProto = rt.bootstrap().objectProto;
    // The value of captured name `name`: in the dict of the innermost
    // activation up the captured chain that binds it (as STORE_CAPTURED
    // finds it). False when no activation binds it.
    auto capturedValue = [&](const std::string& name, const proto::ProtoObject*& v) {
        if (!f.captured || f.captured == PROTO_NONE) return false;
        const proto::ProtoString* sym = proto::ProtoString::createSymbol(ctx, name.c_str());
        for (const proto::ProtoObject* d = f.captured;
             d && d != PROTO_NONE && d != objectProto; d = d->getFirstParent(ctx)) {
            if (d->hasOwnAttribute(ctx, sym) == PROTO_TRUE) {
                v = d->getAttribute(ctx, sym);
                return true;
            }
        }
        return false;
    };
    auto shown = [](const std::string& n) { return !n.empty() && n.rfind("__", 0) != 0; };

    // Slot names, with an inlined loop's counter named after the loop's
    // block argument where the frame is inside that loop.
    // Such a counter goes first: it shadows a variable of the same name.
    std::vector<std::string> slotNames(f.locals.size());
    std::vector<size_t> order;
    for (size_t i = 0; i < slotNames.size(); ++i) slotNames[i] = m->localName(i);
    for (const auto& sl : m->scopedLocals())
        if (sl.slot < slotNames.size() && f.pc >= sl.fromPc && f.pc < sl.toPc) {
            slotNames[sl.slot] = sl.name;
            order.push_back(sl.slot);
        }
    for (size_t i = 0; i < slotNames.size(); ++i)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);

    std::unordered_set<std::string> seen;
    for (size_t i : order) {
        const std::string& n = slotNames[i];
        if (!shown(n) || !seen.insert(n).second) continue;
        const proto::ProtoObject* v = f.locals[i];
        // A temporary a block shares lives in the captured dict; its slot
        // stays nil.
        if (!v || v == PROTO_NONE) capturedValue(n, v);
        out.push_back({n, v ? v : PROTO_NONE});
    }
    // The captured names this code reads or writes without a slot of its
    // own: a block's view of its method's temporaries.
    const auto& bytes = m->bytes();
    for (size_t pc = 0; pc + 1 < bytes.size(); ) {
        Op op = static_cast<Op>(bytes[pc]);
        unsigned int arg = bytes[pc + 1];
        pc += kInstrSize;
        while (op == Op::EXTEND && pc + 1 < bytes.size()) {
            op  = static_cast<Op>(bytes[pc]);
            arg = (arg << 8) | bytes[pc + 1];
            pc += kInstrSize;
        }
        if (op != Op::PUSH_CAPTURED && op != Op::STORE_CAPTURED && op != Op::DEFINE_CAPTURED)
            continue;
        const std::string& n = m->constSymbol(arg);
        if (!shown(n) || seen.count(n)) continue;
        const proto::ProtoObject* v = nullptr;
        if (!capturedValue(n, v)) continue;
        seen.insert(n);
        out.push_back({n, v ? v : PROTO_NONE});
    }
    if (!seen.count("self") && f.self && f.self != PROTO_NONE)
        out.insert(out.begin(), FrameVariable{"self", f.self});
    return out;
}

bool DebuggerRuntime::evaluateValue(STRuntime& rt, proto::ProtoContext* ctx,
                                    const std::string& expr, const DebugFrame* frame,
                                    const std::vector<FrameVariable>& extra,
                                    const proto::ProtoObject*& result, std::string& error) {
    // Stops are off while the expression runs: a breakpoint or an exception
    // filter met by the expression would stop the thread that is evaluating
    // it -- the stopped thread itself, or the adapter's.
    SuppressStops quiet(rt.debugger());
    // The handlers of the stopped program are hidden while it runs: an
    // exception the expression raises must not be caught by an on:do: of the
    // program, which would unwind the program's own frames from inside the
    // debugger. Restored on every exit.
    struct HideHandlers {
        std::vector<proto::proto_ulong> ids = handlerStackDisableAll();
        ~HideHandlers() { handlerStackRestore(ids); }
    } hidden;

    // The frame's named variables (and the extra bindings, which shadow a
    // variable of the same name), bound by value as the parameters of a block
    // that the expression is the body of.
    std::vector<std::string> names;
    std::vector<const proto::ProtoObject*> values;
    std::unordered_set<std::string> taken;
    for (const FrameVariable& v : extra) {
        if (!isIdentifier(v.name) || !taken.insert(v.name).second) continue;
        names.push_back(v.name);
        values.push_back(v.value ? v.value : PROTO_NONE);
    }
    if (frame && frame->module) {
        for (const FrameVariable& v : frameVariables(rt, ctx, *frame)) {
            if (!isIdentifier(v.name) || names.size() >= 64 || !taken.insert(v.name).second) continue;
            names.push_back(v.name);
            values.push_back(v.value ? v.value : PROTO_NONE);
        }
    }
    try {
        std::string err;
        if (!names.empty()) {
            std::string src = "[";
            for (const auto& n : names) src += " :" + n;
            src += " | " + expr + "\n].";
            if (auto bc = compileSource(src, err)) {
                const proto::ProtoObject* block = rt.runTopLevel(*bc, ctx);
                result = invokeBlock(rt, ctx, block, values.data(), static_cast<int>(values.size()));
                if (!result) result = PROTO_NONE;
                return true;
            }
            // Not valid as a block body: evaluate it on its own below.
        }
        // Append the statement terminator protoST expects; the caller passes
        // a bare expression.
        auto bc = compileSource(expr + ".", err);
        if (!bc) {
            error = err;
            return false;
        }
        // Run on the calling thread's own context: a stop inside an actor
        // method evaluates on that worker, never on rootCtx.
        result = rt.runTopLevel(*bc, ctx);
        if (!result) result = PROTO_NONE;
        return true;
    } catch (const proto::ProtoException& e) {
        // The message alone: no "error: " prefix, no stack of the evaluation.
        error = describeUncaught(ctx, e);
        error = error.substr(0, error.find('\n'));
        if (error.rfind("error: ", 0) == 0) error.erase(0, 7);
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        // A non-local return (`^`) or another unwinding carrier: nothing of
        // the program's is on its way, the expression simply has no value.
        error = "the expression returned non-locally";
        return false;
    }
}

bool DebuggerRuntime::evaluateExpression(STRuntime& rt, proto::ProtoContext* ctx,
                                         const std::string& expr,
                                         std::string& out,
                                         const DebugFrame* frame) {
    const proto::ProtoObject* r = nullptr;
    std::string err;
    if (!evaluateValue(rt, ctx, expr, frame, {}, r, err)) {
        out = err;
        return false;
    }
    SuppressStops quiet(rt.debugger());
    // BL-3: shared formatter -- non-primitive objects render as
    // "a ClassName" instead of "<obj>".
    out = protoST::formatValue(rt, ctx, r);
    return true;
}

bool DebuggerRuntime::evaluateCondition(STRuntime& rt, proto::ProtoContext* ctx,
                                        const std::string& condition, const DebugFrame& frame,
                                        const std::vector<FrameVariable>& extra,
                                        std::string& error) {
    const proto::ProtoObject* r = nullptr;
    if (!evaluateValue(rt, ctx, condition, &frame, extra, r, error)) {
        error = "condition `" + condition + "` failed: " + error;
        return true;
    }
    if (r == PROTO_TRUE) return true;
    if (r == PROTO_FALSE) return false;
    SuppressStops quiet(rt.debugger());
    error = "condition `" + condition + "` failed: it answered " +
            protoST::formatValue(rt, ctx, r) + ", not a Boolean";
    return true;
}

std::string DebuggerRuntime::interpolateLogMessage(STRuntime& rt, proto::ProtoContext* ctx,
                                                   const std::string& message,
                                                   const DebugFrame& frame) {
    std::string out;
    for (size_t i = 0; i < message.size(); ) {
        const char c = message[i];
        if (c == '{' && i + 1 < message.size() && message[i + 1] == '{') { out += '{'; i += 2; continue; }
        if (c == '}' && i + 1 < message.size() && message[i + 1] == '}') { out += '}'; i += 2; continue; }
        if (c != '{') { out += c; ++i; continue; }
        const size_t close = message.find('}', i + 1);
        if (close == std::string::npos) { out += message.substr(i); break; }
        const std::string expr = message.substr(i + 1, close - i - 1);
        i = close + 1;
        const proto::ProtoObject* r = nullptr;
        std::string err;
        if (!evaluateValue(rt, ctx, expr, &frame, {}, r, err)) {
            out += "<error: " + err + ">";
            continue;
        }
        SuppressStops quiet(rt.debugger());
        // A string interpolates as its characters, anything else as it prints.
        const proto::ProtoString* s = (r && r != PROTO_NONE && r->isString(ctx)) ? r->asString(ctx) : nullptr;
        out += s ? s->toStdString(ctx) : protoST::formatValue(rt, ctx, r);
    }
    return out;
}

void DebuggerRuntime::enterSession(STRuntime& rt, proto::ProtoContext* ctx,
                                   DebugFrame frame, const std::string& reason,
                                   const StopInfo* info) {
    // F8-3: when a frontend is installed (e.g. the DAP adapter), route the
    // stop through it instead of the built-in text REPL. The frontend blocks
    // until the user resumes this thread and returns the resume command; we
    // apply it to this thread's stepping state and return. The `-d` text path
    // (no frontend) below is unchanged.
    if (DebuggerFrontend* fe = frontend_.load()) {
        DebugThreadState* ts = currentThread(ctx);
        if (!ts->registered) registerThread(ts, ctx);
        StopInfo si = info ? *info : StopInfo{};
        if (si.reason.empty()) si.reason = reason;
        si.threadId   = ts->id;
        si.threadName = threadDisplayName(rt, ctx, *ts);
        Command cmd = fe->onStopped(rt, ctx, frame, si);
        setCommand(cmd);
        applyCommand(*ts, cmd, frame);
        return;
    }

    auto& out = outStream_ ? *outStream_ : std::cout;
    auto& in  = inStream_  ? *inStream_  : std::cin;

    out << "halted: " << reason << "\n";
    out << "  pc=" << frame.pc << " stack=" << frame.stack.size()
        << " locals=" << frame.locals.size() << "\n";

    while (true) {
        out << "(stdbg) " << std::flush;
        std::string line;
        // 2026-05-25: bracket the user-input read in an unmanaged
        // region. The debugger is sitting at the (stdbg) prompt
        // waiting for the user, but worker threads may still be alive
        // (an actor pool from before the break). Without the bracket,
        // a GC cycle triggered by another thread would stall behind
        // this read for as long as the user thinks. The region is opened on
        // the halted thread's own context, which may be a worker's.
        bool ok;
        {
            proto::ProtoContext::UnmanagedScope u(ctx);
            ok = static_cast<bool>(std::getline(in, line));
        }
        if (!ok) break;
        line = trim(line);
        if (line.empty()) continue;

        if (line == "c" || line == "cont")    { setMode(Mode::Free);        setCommand(Command::Continue); return; }
        // F2: `next` is aliased to `step` because true frame-depth tracking
        // for `next` requires the F3 call stack (not yet implemented).
        if (line == "s" || line == "step")    { setMode(Mode::SingleStep);  setCommand(Command::Step);     return; }
        if (line == "n" || line == "next")    { setMode(Mode::SingleStep);  setCommand(Command::Next);     return; }
        // F2: `finish` falls back to Free (same as cont) because method
        // call frames aren't yet implemented. True RunToReturn requires F3.
        if (line == "f" || line == "finish")  { setMode(Mode::RunToReturn); setCommand(Command::Finish);   return; }
        if (line == "q" || line == "quit")    { std::exit(0); }

        if (line == "where" || line == "bt") {
            out << "frame depth: " << frame.frameDepth << "\n";
            out << "pc: "          << frame.pc << " / " << frame.module->bytes().size() << "\n";
            // Dump the next 6 instructions. BL-2: instructions are variable
            // width (an EXTEND prefix word per extra operand byte), so step
            // by the real decoded width rather than a fixed 2 bytes.
            const auto& dbg = frame.module->bytes();
            size_t cur = frame.pc;
            for (size_t k = 0; k < 6 && cur + 1 < dbg.size(); ++k) {
                size_t start = cur;
                Op op = static_cast<Op>(dbg[cur]);
                unsigned int arg = dbg[cur + 1];
                cur += 2;
                while (op == Op::EXTEND && cur + 1 < dbg.size()) {
                    op  = static_cast<Op>(dbg[cur]);
                    arg = (arg << 8) | dbg[cur + 1];
                    cur += 2;
                }
                out << "  " << start << ": op=" << static_cast<int>(op)
                    << " arg=" << static_cast<int>(arg) << "\n";
            }
            continue;
        }
        if (line == "locals") {
            for (size_t k = 0; k < frame.locals.size(); ++k) {
                // BL-3: shared formatter — render objects as "a ClassName".
                out << "  [" << k << "] "
                    << protoST::formatValue(rt, ctx, frame.locals[k])
                    << "\n";
            }
            continue;
        }
        if (line.rfind("print ", 0) == 0 || line.rfind("p ", 0) == 0) {
            // F8-4: parse -> compile -> run -> format now lives in the shared
            // evaluateExpression helper, reused by the DAP `evaluate` request.
            std::string result;
            evaluateExpression(rt, ctx, line.substr(line.find(' ') + 1), result, &frame);
            out << "  " << result << "\n";
            continue;
        }

        if (line.rfind("break ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                breakpoints_.add(frame.module, bpc);
                out << "  break set at pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }
        if (line == "info breaks") {
            out << "  " << breakpoints_.size() << " breakpoints set\n";
            continue;
        }
        if (line.rfind("clear ", 0) == 0) {
            try {
                size_t bpc = std::stoul(line.substr(6));
                breakpoints_.remove(frame.module, bpc);
                out << "  cleared pc=" << bpc << "\n";
            } catch (...) { out << "  invalid pc\n"; }
            continue;
        }

        out << "?? unknown command: " << line << " (try: c, s, n, f, q, where, locals, print, break, clear, info breaks)\n";
    }
}

} // namespace protoST
