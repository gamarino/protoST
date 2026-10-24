#include "dap/DapServer.h"
#include "runtime/UnhandledSTException.h"
#include "dap/DapStdio.h"
#include "dap/DapTransport.h"

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/ValueFormat.h"
#include "debugger/DebuggerRuntime.h"
#include "protoCore.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace protoST {

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------
// The DAP server (F8-2 lifecycle skeleton + F8-3 launch / breakpoints /
// stop-resume handshake / stepping, exception breakpoints, threads).
//
// Threading model:
//   * The DAP message loop (`run`) runs on the main thread and NEVER blocks on
//     a stopped thread. It must keep reading stdin while threads are stopped,
//     otherwise it could not receive `continue`.
//   * `launch` spawns the *debuggee thread* ("main", DAP thread 1): a protoCore
//     ProtoThread of the runtime's space with its own ProtoContext. It waits
//     for the configurationDone gate, then runs `rt.runTopLevel(module, ctx)`
//     on that context. The DAP loop thread constructed the runtime and owns
//     `rootCtx`; the two threads never share a context (S4).
//   * Every other thread that runs script code -- the actor worker pool, and
//     the main thread itself while it drains actors in `wait` -- is a DAP
//     thread too: the debugger registers it the first time it runs an
//     instruction (a `thread` started event) and reports its exit.
//   * One-thread stop: a thread that stops (breakpoint, step, pause,
//     exception) stops alone (`allThreadsStopped: false`); the others keep
//     running. Each stopped thread parks in `onStopped` on its own condition
//     variable, inside an unmanaged region on its own context, so collections
//     go on while it waits. Several threads may be stopped at once.
//   * Inspection runs ON the stopped thread that owns the values: `variables`
//     and `evaluate` queue a task to that thread, which runs it on its own
//     context (managed again for the duration) and hands the result back. The
//     DAP thread waits for it in an unmanaged region, with a timeout.
//   * A resume request (`continue`/`next`/`stepIn`/`stepOut`) names a thread:
//     it records that thread's command and wakes only that thread.
class DapServer : public DebuggerFrontend {
public:
    DapServer() : transport_(stdio_.protocolIn(), stdio_.protocolOut()) {}
    ~DapServer() override {
        shutdownDebuggee();
        // Its threads write to transport_. The debuggee is joined and no
        // thread is inside onStopped: only this thread can still be
        // registered.
        stdio_.finish(runtime_ ? runtime_->rootCtx() : nullptr);
        // The runtime joins its workers; none of them reaches this server any
        // more (the frontend is uninstalled), but destroy it while every
        // member is still alive.
        runtime_.reset();
    }

    int run() {
        // What the debuggee prints reaches the client's Debug Console as
        // `output` events instead of corrupting the protocol stream.
        stdio_.startForwarding([this](const char* category, const std::string& text) {
            sendOutput(category, text);
        });
        for (;;) {
            // 2026-05-25: the DAP main loop is the prototypical
            // "external event source" — the IDE sends a message when
            // it wants something, and the wait can be arbitrarily
            // long. Without an unmanaged-region bracket the GC quorum
            // stalls behind us, freezing the entire debuggee while we
            // sit idle on stdin. The bracket is conditional on the
            // runtime existing (it does not yet on the first iteration,
            // before `launch` constructs `runtime_`).
            std::optional<json> msg;
            if (runtime_) {
                proto::ProtoContext::UnmanagedScope u(runtime_->rootCtx());
                msg = transport_.readMessage();
            } else {
                msg = transport_.readMessage();
            }
            if (!msg)
                break;                       // EOF / stream error / malformed
            const json& m = *msg;
            std::string type = m.value("type", "");
            if (type != "request") {
                std::cerr << "[dap] ignoring non-request message of type '"
                          << type << "'\n";
                continue;
            }
            if (handleRequest(m))
                break;                       // handler asked to stop the loop
        }
        shutdownDebuggee();
        return exitCode_;
    }

    // --- DebuggerFrontend ---------------------------------------------------
    // Called on the thread that stopped. Emits a `stopped` event for that
    // thread alone, then parks it -- running the inspection tasks queued to
    // it meanwhile -- until a resume request for it (or the shutdown) arrives.
    DebuggerRuntime::Command onStopped(STRuntime& /*rt*/, proto::ProtoContext* ctx,
                                       const DebugFrame& frame,
                                       const StopInfo& info) override {
        // The thread blocks below for as long as the user takes, in an
        // unmanaged region on its own context, opened before any lock so the
        // locks are released before the region's exit can park. It leaves the
        // region only to run an inspection task. Nothing else here
        // dereferences a ProtoObject: the frame's values are only copied as
        // pointers and stay rooted in the stopped engines' slots.
        std::optional<proto::ProtoContext::UnmanagedScope> unmanaged;
        unmanaged.emplace(ctx);

        // A line printed without a newline is still in stdio's buffer.
        std::fflush(stdout);

        std::unique_lock<std::mutex> lk(sessionMu_);
        if (shuttingDown_)
            return DebuggerRuntime::Command::Continue;
        ThreadRec& r = threadRec(info.threadId);
        r.name        = info.threadName;
        r.stopped     = true;
        r.frame       = frame;
        r.info        = info;
        r.resumeReady = false;
        ++inStop_;
        lk.unlock();

        json body = {
            {"reason",            info.reason},
            {"threadId",          info.threadId},
            {"allThreadsStopped", false},
        };
        if (!info.description.empty()) {
            body["description"] = info.description;
            body["text"]        = info.description;
        }
        sendEvent("stopped", body);

        lk.lock();
        for (;;) {
            r.cv.wait(lk, [&]{ return r.resumeReady || !r.tasks.empty(); });
            if (r.resumeReady)
                break;
            // An inspection task: run it on this thread's own context, managed.
            auto task = std::move(r.tasks.front());
            r.tasks.pop_front();
            lk.unlock();
            unmanaged.reset();
            task(ctx);
            unmanaged.emplace(ctx);
            lk.lock();
        }
        DebuggerRuntime::Command cmd = r.cmd;
        r.stopped = false;
        r.name    = r.baseName;
        r.tasks.clear();
        dropHandlesOf(r.id);
        if (--inStop_ == 0)
            stopsDrainedCv_.notify_all();
        return cmd;
    }

    void onOutput(const std::string& category, const std::string& text) override {
        sendOutput(category, text);
    }

    void onThreadEvent(int threadId, const std::string& name, bool started) override {
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            if (started) {
                ThreadRec& r = threadRec(threadId);
                r.baseName = name;
                if (!r.stopped) r.name = name;
            } else {
                auto it = threads_.find(threadId);
                if (it != threads_.end() && !it->second->stopped)
                    threads_.erase(it);
            }
            if (terminatedSent_)
                return;
        }
        sendEvent("thread", { {"reason", started ? "started" : "exited"},
                              {"threadId", threadId} });
    }

private:
    // One thread of the debuggee, as the client sees it. Guarded by
    // sessionMu_; owned by threads_ (stable address).
    struct ThreadRec {
        int                     id = 0;
        std::string             baseName;
        std::string             name;
        bool                    stopped = false;
        DebugFrame              frame;          // the stop's frame (valid while stopped)
        StopInfo                info;           // why it stopped
        bool                    resumeReady = false;
        DebuggerRuntime::Command cmd = DebuggerRuntime::Command::Continue;
        std::deque<std::function<void(proto::ProtoContext*)>> tasks;
        std::condition_variable cv;
    };

    // Requires sessionMu_.
    ThreadRec& threadRec(int id) {
        auto& p = threads_[id];
        if (!p) {
            p = std::make_unique<ThreadRec>();
            p->id = id;
            p->baseName = p->name = (id == 1 ? "main" : "thread " + std::to_string(id));
        }
        return *p;
    }

    // Requires sessionMu_: forget the frame / variables handles of a thread
    // that resumes.
    void dropHandlesOf(int threadId) {
        for (auto it = frameTable_.begin(); it != frameTable_.end(); )
            it = (it->second.threadId == threadId) ? frameTable_.erase(it) : std::next(it);
        for (auto it = varRefTable_.begin(); it != varRefTable_.end(); )
            it = (it->second.threadId == threadId) ? varRefTable_.erase(it) : std::next(it);
    }

    // Runs `fn(ctx)` on stopped thread `threadId`, on its own context, and
    // waits for it. False when that thread is not stopped, or when the task
    // did not finish in time (it then still runs; its result is dropped).
    bool runOnStoppedThread(int threadId, std::function<void(proto::ProtoContext*)> fn) {
        struct Done {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
        };
        auto done = std::make_shared<Done>();
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            auto it = threads_.find(threadId);
            if (it == threads_.end() || !it->second->stopped || it->second->resumeReady)
                return false;
            it->second->tasks.push_back([done, fn = std::move(fn)](proto::ProtoContext* ctx) {
                try { fn(ctx); } catch (...) {}
                {
                    std::lock_guard<std::mutex> l(done->m);
                    done->done = true;
                }
                done->cv.notify_all();
            });
            it->second->cv.notify_all();
        }
        // The wait blocks this thread, which owns rootCtx.
        proto::ProtoContext::UnmanagedScope unmanaged(runtime_->rootCtx());
        std::unique_lock<std::mutex> l(done->m);
        return done->cv.wait_for(l, std::chrono::seconds(30), [&]{ return done->done; });
    }

    // ----------------------------------------------------------------------
    // Returns true when the message loop should terminate.
    bool handleRequest(const json& req) {
        std::string command = req.value("command", "");

        // ---- F8-2: lifecycle skeleton -------------------------------------
        if (command == "initialize")       { handleInitialize(req);       return false; }
        if (command == "disconnect" || command == "terminate")
                                           { handleDisconnect(req);       return true;  }

        // ---- F8-3: launch / breakpoints / session / stepping --------------
        if (command == "launch")           { handleLaunch(req);           return false; }
        if (command == "setBreakpoints")   { handleSetBreakpoints(req);   return false; }
        if (command == "setExceptionBreakpoints") { handleSetExceptionBreakpoints(req); return false; }
        if (command == "configurationDone"){ handleConfigurationDone(req);return false; }
        if (command == "threads")          { handleThreads(req);          return false; }
        if (command == "continue" || command == "next" ||
            command == "stepIn"   || command == "stepOut") {
            handleResume(req, command);
            return false;
        }
        if (command == "pause")            { handlePause(req);            return false; }

        // ---- F8-4: stackTrace, scopes, variables, evaluate ----------------
        if (command == "stackTrace")    { handleStackTrace(req);    return false; }
        if (command == "scopes")        { handleScopes(req);        return false; }
        if (command == "variables")     { handleVariables(req);     return false; }
        if (command == "evaluate")      { handleEvaluate(req);      return false; }
        if (command == "exceptionInfo") { handleExceptionInfo(req); return false; }

        std::cerr << "[dap] unhandled command: " << command << '\n';
        sendResponse(req, /*success=*/true, json::object());
        return false;
    }

    void handleInitialize(const json& req) {
        json caps = {
            {"supportsConfigurationDoneRequest", true},
            {"supportsExceptionInfoRequest",     true},
            {"supportsExceptionFilterOptions",   true},
            {"supportsConditionalBreakpoints",   true},
            {"supportsHitConditionalBreakpoints", true},
            {"supportsLogPoints",                true},
            {"exceptionBreakpointFilters", json::array({
                json{{"filter", "raised"},
                     {"label", "Raised Exceptions"},
                     {"description", "Stop when an exception is signalled, before any handler runs."},
                     {"default", false},
                     {"supportsCondition", true},
                     {"conditionDescription", "A Smalltalk expression; the exception is ex, e.g. ex isKindOf: ZeroDivide"}},
                json{{"filter", "uncaught"},
                     {"label", "Uncaught Exceptions"},
                     {"description", "Stop when an exception that no on:do: handler will catch is signalled, with the stack intact."},
                     {"default", true},
                     {"supportsCondition", true},
                     {"conditionDescription", "A Smalltalk expression; the exception is ex, e.g. ex isKindOf: ZeroDivide"}},
            })},
        };
        sendResponse(req, /*success=*/true, caps);
        sendEvent("initialized", json::object());
    }

    // --- setExceptionBreakpoints --------------------------------------------
    void handleSetExceptionBreakpoints(const json& req) {
        const json& args = req.value("arguments", json::object());
        unsigned bits = 0;
        std::string raisedCond, uncaughtCond;
        json verified = json::array();
        auto add = [&](const std::string& f, const std::string& cond) {
            const bool known = f == "raised" || f == "uncaught";
            if (f == "raised")        { bits |= DebuggerRuntime::kFilterRaised;   raisedCond = cond; }
            else if (f == "uncaught") { bits |= DebuggerRuntime::kFilterUncaught; uncaughtCond = cond; }
            json v = { {"verified", known} };
            std::string err;
            if (known && !cond.empty() && !conditionCompiles(cond, "ex", err)) {
                // Reported now; the condition still stops (with the error)
                // when its exception is raised.
                sendOutput("stderr", "exception breakpoint condition `" + cond + "` failed: " + err + "\n");
                v["message"] = err;
            }
            verified.push_back(v);
        };
        if (args.contains("filters") && args["filters"].is_array())
            for (const auto& f : args["filters"])
                if (f.is_string()) add(f.get<std::string>(), "");
        if (args.contains("filterOptions") && args["filterOptions"].is_array())
            for (const auto& o : args["filterOptions"])
                add(o.value("filterId", ""), o.value("condition", ""));
        exceptionFilters_  = bits;
        raisedCondition_   = raisedCond;
        uncaughtCondition_ = uncaughtCond;
        if (runtime_) {
            runtime_->debugger().setExceptionConditions(raisedCond, uncaughtCond);
            runtime_->debugger().setExceptionFilters(bits);
        }
        sendResponse(req, /*success=*/true, { {"breakpoints", verified} });
    }

    // --- launch -------------------------------------------------------------
    void handleLaunch(const json& req) {
        const json& args = req.value("arguments", json::object());
        std::string program = args.value("program", "");
        bool stopOnEntry    = args.value("stopOnEntry", false);

        if (program.empty()) {
            sendErrorResponse(req, "launch: missing 'program'");
            return;
        }

        // Read the script file.
        std::FILE* fp = std::fopen(program.c_str(), "rb");
        if (!fp) {
            sendOutput("stderr", "cannot open " + program + "\n");
            sendErrorResponse(req, "launch: cannot open program");
            sendTerminated();
            return;
        }
        // Read in chunks, without asking for the size: ftell answers a long,
        // which is 32 bits on Windows.
        std::string src;
        {
            char chunk[65536];
            size_t got;
            while ((got = std::fread(chunk, 1, sizeof chunk, fp)) > 0) src.append(chunk, got);
        }
        std::fclose(fp);

        // Parse + compile.
        Parser parser(std::move(src));
        auto ast = parser.parseModule();
        if (!parser.errors().empty()) {
            for (const auto& e : parser.errors()) {
                sendOutput("stderr", program + ":" + std::to_string(e.line) + ":" +
                                     std::to_string(e.column) + ": " + e.message + "\n");
            }
            sendErrorResponse(req, "launch: parse error");
            sendTerminated();
            return;
        }
        Compiler compiler;
        auto module = compiler.compileModule(*ast);
        if (compiler.hasErrors()) {
            for (const auto& s : compiler.errors())
                sendOutput("stderr", "compile error: " + s + "\n");
            sendErrorResponse(req, "launch: compile error");
            sendTerminated();
            return;
        }
        module->setSourceName(program);

        // Retain the module + runtime for the session lifetime.
        module_      = std::move(module);
        runtime_     = std::make_unique<STRuntime>();
        stopOnEntry_ = stopOnEntry;
        runtime_->debugger().setFrontend(this);
        runtime_->debugger().setExceptionConditions(raisedCondition_, uncaughtCondition_);
        runtime_->debugger().setExceptionFilters(exceptionFilters_);
        runtime_->debugger().attach();

        // Re-resolve any breakpoints requested before the module existed.
        rearmAllBreakpoints();

        // Spawn the debuggee as a ProtoThread of the runtime's space, so it
        // runs on its own context and takes part in the stop-the-world
        // protocol like any other thread of the space.
        {
            proto::ProtoContext* rctx = runtime_->rootCtx();
            const proto::ProtoList* threadArgs = rctx->newList()->appendLast(
                rctx, rctx->fromExternalPointer(this, nullptr));
            debuggeeThread_ = runtime_->space()->newThread(
                rctx,
                proto::ProtoString::createSymbol(rctx, "protoST-dap-debuggee"),
                &DapServer::debuggeeEntry, threadArgs, nullptr);
        }
        if (!debuggeeThread_) {
            sendErrorResponse(req, "launch: cannot start the debuggee thread");
            sendTerminated();
            return;
        }

        sendResponse(req, /*success=*/true, json::object());
    }

    // ProtoMethod trampoline for the debuggee ProtoThread: args[0] is an
    // ExternalPointer to this server.
    static const proto::ProtoObject* debuggeeEntry(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* /*self*/,
        const proto::ParentLink* /*parentLink*/,
        const proto::ProtoArguments* args,
        const proto::ProtoKeywordArguments* /*kwargs*/) {
        if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
        const proto::ProtoObject* first = args->getAt(ctx, 0);
        const proto::ProtoExternalPointer* ep =
            first ? first->asExternalPointer(ctx) : nullptr;
        auto* server = ep ? static_cast<DapServer*>(ep->getPointer(ctx)) : nullptr;
        if (server) server->debuggeeMain(ctx);
        return PROTO_NONE;
    }

    // The debuggee thread entry point. `ctx` is the thread's own context.
    void debuggeeMain(proto::ProtoContext* ctx) {
        // The script's own thread is DAP thread 1, "main": registered before
        // any other thread can run script code.
        runtime_->debugger().nameCurrentThread("main", ctx);

        // Gate: wait until `configurationDone` so breakpoints set between
        // `launch` and `configurationDone` are armed before the first
        // instruction runs. The wait is an unmanaged region.
        {
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            std::unique_lock<std::mutex> lk(sessionMu_);
            configDoneCv_.wait(lk, [&]{ return configDone_ || shuttingDown_; });
            if (shuttingDown_) {
                debuggeeDone_ = true;
                debuggeeDoneCv_.notify_all();
                return;
            }
        }

        // stopOnEntry: stop once with reason "entry" before the first real
        // instruction, on a synthetic frame at pc 0 of the top module. Through
        // the debugger, which applies the resume command to this thread.
        if (stopOnEntry_ && runtime_->debugger().attached()) {
            DebugFrame entryFrame;
            entryFrame.module = module_.get();
            entryFrame.pc     = 0;
            entryFrame.line   = module_->lineForPc(0);
            runtime_->debugger().enterSession(*runtime_, ctx, entryFrame, "entry");
        }

        int exitCode = 0;
        std::string error;
        try {
            runtime_->runTopLevel(*module_, ctx);
        } catch (const proto::ProtoException& e) {
            exitCode = 1;
            error = describeUncaught(ctx, e) + "\n";
        } catch (const std::exception& e) {
            exitCode = 1;
            error = std::string("error: ") + e.what() + "\n";
        } catch (...) {
            exitCode = 1;
            error = "error: unknown exception\n";
        }

        // Writing to the client can block on a full pipe; no ProtoObject is
        // touched, so the writes run in an unmanaged region.
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        stdio_.finish(ctx);                  // the script's output goes first
        if (!error.empty())
            sendOutput("stderr", error);
        sendTerminated();
        sendEvent("exited", { {"exitCode", exitCode} });
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            debuggeeDone_ = true;
        }
        debuggeeDoneCv_.notify_all();
    }

    // --- setBreakpoints -----------------------------------------------------
    void handleSetBreakpoints(const json& req) {
        const json& args = req.value("arguments", json::object());
        std::string path = args.value("source", json::object()).value("path", "");

        // Each breakpoint's condition / hitCondition / logMessage; one whose
        // hitCondition does not parse is reported unverified and not armed.
        std::vector<RequestedBreakpoint> bps;
        std::vector<std::string> problems;
        if (args.contains("breakpoints")) {
            for (const auto& bp : args["breakpoints"]) {
                RequestedBreakpoint r;
                r.line = bp.value("line", 0);
                auto spec = std::make_shared<BreakpointSpec>();
                spec->condition = bp.value("condition", "");
                std::string problem;
                if (!spec->parseHitCondition(bp.value("hitCondition", ""), problem))
                    r.valid = false;
                if (bp.contains("logMessage") && bp["logMessage"].is_string()) {
                    spec->isLogpoint = true;
                    spec->logMessage = bp["logMessage"].get<std::string>();
                }
                std::string err;
                if (r.valid && !spec->condition.empty() && !conditionCompiles(spec->condition, "", err)) {
                    problem = "condition: " + err;
                    sendOutput("stderr", "breakpoint condition `" + spec->condition + "` failed: " + err + "\n");
                }
                r.spec = spec;
                bps.push_back(r);
                problems.push_back(problem);
            }
        }

        // setBreakpoints REPLACES all breakpoints for this source.
        requestedBreakpoints_[path] = bps;

        // (Re)resolve against the module if it already exists. Before launch
        // (VS Code may send setBreakpoints first) they are unverified for
        // now and armed in launch.
        json result = json::array();
        for (size_t k = 0; k < bps.size(); ++k) {
            size_t pc = SIZE_MAX;
            bool verified = module_ && runtime_ && bps[k].valid &&
                            resolveLineToModule(bps[k].line, pc) != nullptr;
            json r = { {"verified", verified}, {"line", bps[k].line} };
            if (!problems[k].empty()) r["message"] = problems[k];
            result.push_back(r);
        }
        if (module_ && runtime_) {
            // Rebuild the whole breakpoint table from all sources, since
            // BreakpointTable has no per-source clear.
            rearmAllBreakpoints();
        }

        sendResponse(req, /*success=*/true, { {"breakpoints", result} });
    }

    // Rebuilds the BreakpointTable from every requested source's lines, in
    // one step: running threads never see it half built.
    void rearmAllBreakpoints() {
        if (!module_ || !runtime_)
            return;
        std::vector<BreakpointTable::Location> entries;
        for (const auto& [path, bps] : requestedBreakpoints_) {
            (void)path;
            for (const RequestedBreakpoint& bp : bps) {
                if (!bp.valid) continue;
                size_t pc = SIZE_MAX;
                const BytecodeModule* m = resolveLineToModule(bp.line, pc);
                if (m)
                    entries.push_back({m, pc, bp.spec});
            }
        }
        runtime_->debugger().breakpoints().assign(entries);
    }

    // Resolves a source line to the (sub-)module that owns an instruction on
    // that line and writes its pc. Returns nullptr when no instruction maps to
    // the line. A breakpoint line may be inside a method/block sub-module, so
    // the search descends into sub-blocks.
    const BytecodeModule* resolveLineToModule(int line, size_t& outPc) {
        outPc = SIZE_MAX;
        if (!module_)
            return nullptr;
        return searchModuleForLine(module_.get(), line, outPc);
    }

    // Depth-first search of a module and its sub-blocks for the first
    // instruction mapping to `line`. A breakpoint line may be inside a
    // method/block sub-module, so we must descend.
    static const BytecodeModule* searchModuleForLine(const BytecodeModule* m,
                                                     int line, size_t& outPc) {
        if (!m)
            return nullptr;
        size_t pc = m->firstPcForLine(line);
        if (pc != SIZE_MAX) {
            outPc = pc;
            return m;
        }
        for (size_t i = 0; i < m->numBlocks(); ++i) {
            const BytecodeModule* found =
                searchModuleForLine(&m->block(i), line, outPc);
            if (found)
                return found;
        }
        return nullptr;
    }

    // --- configurationDone --------------------------------------------------
    void handleConfigurationDone(const json& req) {
        sendResponse(req, /*success=*/true, json::object());
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            configDone_ = true;
        }
        configDoneCv_.notify_all();
    }

    // --- threads ------------------------------------------------------------
    void handleThreads(const json& req) {
        json list = json::array();
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            for (const auto& [id, r] : threads_)
                list.push_back({ {"id", id}, {"name", r->name} });
        }
        // Before the script's thread is registered, show it anyway.
        if (list.empty())
            list.push_back({ {"id", 1}, {"name", "main"} });
        sendResponse(req, /*success=*/true, { {"threads", list} });
    }

    // --- continue / next / stepIn / stepOut --------------------------------
    // Acts on the request's thread only; the other stopped threads stay
    // stopped.
    void handleResume(const json& req, const std::string& command) {
        const json& args = req.value("arguments", json::object());
        int threadId = args.value("threadId", 0);
        DebuggerRuntime::Command cmd = DebuggerRuntime::Command::Continue;
        if (command == "continue")      cmd = DebuggerRuntime::Command::Continue;
        else if (command == "next")     cmd = DebuggerRuntime::Command::Next;
        else if (command == "stepIn")   cmd = DebuggerRuntime::Command::Step;
        else if (command == "stepOut")  cmd = DebuggerRuntime::Command::Finish;

        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            auto it = threads_.find(threadId);
            if (it != threads_.end() && it->second->stopped) {
                it->second->cmd         = cmd;
                it->second->resumeReady = true;
                it->second->cv.notify_all();
            }
        }

        if (command == "continue")
            sendResponse(req, /*success=*/true, { {"allThreadsContinued", false} });
        else
            sendResponse(req, /*success=*/true, json::object());
    }

    // --- pause --------------------------------------------------------------
    // The thread stops at its next instruction. One blocked in native code
    // (a wait, I/O) stops when it is back in the interpreter: the `stopped`
    // event comes then.
    void handlePause(const json& req) {
        const json& args = req.value("arguments", json::object());
        int threadId = args.value("threadId", 0);
        if (runtime_)
            runtime_->debugger().requestPause(threadId);
        sendResponse(req, /*success=*/true, json::object());
    }

    // --- F8-4: inspection ---------------------------------------------------
    //
    // Handle scheme. Every stackTrace / scopes call hands out fresh integer
    // handles from counters shared by all threads, so a handle names one
    // thread's frame unambiguously:
    //   * frameId  — one DebugFrame of a stopped thread's call stack; used as
    //                `frameId` by scopes / evaluate.
    //   * variablesReference — the Locals of such a frame. 0 means not
    //                expandable.
    // A thread's handles are dropped when it resumes; a stale handle resolves
    // to nothing and yields an empty response (VS Code tolerates it).

    // Basename of a path (last component after '/' or '\\').
    static std::string basename(const std::string& path) {
        size_t p = path.find_last_of("/\\");
        return p == std::string::npos ? path : path.substr(p + 1);
    }

    // --- stackTrace ---------------------------------------------------------
    // Built from the stop's snapshot alone (no ProtoObject is touched), on
    // this thread.
    void handleStackTrace(const json& req) {
        const json& args = req.value("arguments", json::object());
        int threadId   = args.value("threadId", 0);
        int startFrame = args.value("startFrame", 0);
        int levels     = args.value("levels", 0);   // 0 == all

        std::lock_guard<std::mutex> lk(sessionMu_);
        std::vector<DebugFrame> stack;
        auto it = threads_.find(threadId);
        if (it != threads_.end() && it->second->stopped) {
            const DebugFrame& top = it->second->frame;
            if (!top.callStack.empty())
                stack = top.callStack;             // oldest-first
            else
                stack.push_back(top);              // single-frame
        }

        json frames = json::array();
        int total = static_cast<int>(stack.size());
        // DAP convention: frame 0 is the innermost (current) frame. The
        // snapshot is oldest-first, so walk it backwards.
        for (int i = startFrame; i < total; ++i) {
            if (levels > 0 && static_cast<int>(frames.size()) >= levels)
                break;
            const DebugFrame& df = stack[static_cast<size_t>(total - 1 - i)];

            int frameId = nextHandle_++;
            frameTable_[frameId] = FrameRef{threadId, df};

            const BytecodeModule* mod = df.module;
            std::string srcPath = mod ? mod->sourceName() : std::string();
            std::string name = (mod && !mod->debugName().empty())
                ? mod->debugName()
                : ("frame " + std::to_string(i));

            json frame = {
                {"id",     frameId},
                {"name",   name},
                {"line",   df.line},
                {"column", 1},
            };
            if (!srcPath.empty()) {
                frame["source"] = {
                    {"name", basename(srcPath)},
                    {"path", srcPath},
                };
            }
            frames.push_back(std::move(frame));
        }

        sendResponse(req, /*success=*/true, {
            {"stackFrames", frames},
            {"totalFrames", total},
        });
    }

    // --- scopes -------------------------------------------------------------
    void handleScopes(const json& req) {
        const json& args = req.value("arguments", json::object());
        int frameId = args.value("frameId", 0);

        json scopes = json::array();
        std::lock_guard<std::mutex> lk(sessionMu_);
        auto it = frameTable_.find(frameId);
        if (it != frameTable_.end()) {
            int localsRef = nextHandle_++;
            varRefTable_[localsRef] = VarRef{it->second.threadId, frameId};
            scopes.push_back({
                {"name",               "Locals"},
                {"variablesReference", localsRef},
                {"expensive",          false},
            });
        }
        sendResponse(req, /*success=*/true, { {"scopes", scopes} });
    }

    // --- variables ----------------------------------------------------------
    // The values are formatted on the stopped thread that owns them.
    void handleVariables(const json& req) {
        const json& args = req.value("arguments", json::object());
        int ref = args.value("variablesReference", 0);

        int threadId = 0;
        DebugFrame df;
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            auto rit = varRefTable_.find(ref);
            if (rit != varRefTable_.end()) {
                auto fit = frameTable_.find(rit->second.frameId);
                if (fit != frameTable_.end()) {
                    threadId = fit->second.threadId;
                    df       = fit->second.frame;
                }
            }
        }
        auto vars = std::make_shared<json>(json::array());
        if (threadId != 0) {
            runOnStoppedThread(threadId, [this, vars, df](proto::ProtoContext* ctx) {
                json out = json::array();
                for (const FrameVariable& v : DebuggerRuntime::frameVariables(*runtime_, ctx, df)) {
                    out.push_back({
                        {"name",               v.name},
                        {"value",              protoST::formatValue(*runtime_, ctx, v.value)},
                        {"variablesReference", 0},
                    });
                }
                *vars = std::move(out);
            });
        }
        sendResponse(req, /*success=*/true, { {"variables", *vars} });
    }

    // True if `s` is a single bare identifier (letters/digits/underscore,
    // not starting with a digit). Used to short-circuit `evaluate` of a watch
    // expression that is just a variable name.
    static bool isBareIdentifier(const std::string& s) {
        if (s.empty()) return false;
        unsigned char c0 = static_cast<unsigned char>(s[0]);
        if (!(std::isalpha(c0) || c0 == '_')) return false;
        for (char ch : s) {
            unsigned char c = static_cast<unsigned char>(ch);
            if (!(std::isalnum(c) || c == '_')) return false;
        }
        return true;
    }

    // --- evaluate -----------------------------------------------------------
    // Runs on the stopped thread that owns the frame (with the frame's named
    // locals in scope); without a frame, on the first stopped thread at its
    // innermost frame; with no thread stopped, on this thread.
    void handleEvaluate(const json& req) {
        const json& args = req.value("arguments", json::object());
        std::string expr = args.value("expression", "");
        int frameId = args.value("frameId", 0);

        if (!runtime_) {
            sendErrorResponse(req, "evaluate: no active runtime");
            return;
        }

        int threadId = 0;
        DebugFrame df;
        bool haveFrame = false;
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            auto fit = frameTable_.find(frameId);
            if (fit != frameTable_.end()) {
                threadId  = fit->second.threadId;
                df        = fit->second.frame;
                haveFrame = true;
            } else {
                for (const auto& [id, r] : threads_) {
                    if (r->stopped) {
                        threadId = id;
                        df = r->frame;
                        if (!df.callStack.empty()) {
                            DebugFrame inner = df.callStack.back();
                            df = std::move(inner);
                        }
                        haveFrame = df.module != nullptr;
                        break;
                    }
                }
            }
        }

        struct Result { bool ok = false; std::string text = "evaluate: the thread did not answer"; };
        auto result = std::make_shared<Result>();
        auto evaluate = [this, result, expr, df, haveFrame](proto::ProtoContext* ctx) {
            // A bare identifier naming a variable of the frame resolves
            // directly against the frame (`self` included).
            if (haveFrame && isBareIdentifier(expr) && df.module) {
                for (const FrameVariable& v : DebuggerRuntime::frameVariables(*runtime_, ctx, df)) {
                    if (v.name == expr) {
                        result->text = protoST::formatValue(*runtime_, ctx, v.value);
                        result->ok = true;
                        return;
                    }
                }
            }
            std::string out;
            result->ok = DebuggerRuntime::evaluateExpression(
                *runtime_, ctx, expr, out, haveFrame ? &df : nullptr);
            result->text = out;
        };
        if (threadId == 0 || !runOnStoppedThread(threadId, evaluate)) {
            if (threadId != 0) {
                sendErrorResponse(req, "evaluate: the thread is not stopped or did not answer");
                return;
            }
            // No thread stopped: run on this thread, which owns rootCtx.
            evaluate(runtime_->rootCtx());
        }
        if (!result->ok) {
            sendErrorResponse(req, result->text);
            return;
        }
        // sendResponse already wraps the argument in the message's `body`;
        // pass the evaluate body fields directly (no extra nesting).
        sendResponse(req, /*success=*/true, {
            {"result",             result->text},
            {"variablesReference", 0},
        });
    }

    // --- exceptionInfo ------------------------------------------------------
    void handleExceptionInfo(const json& req) {
        const json& args = req.value("arguments", json::object());
        int threadId = args.value("threadId", 0);
        StopInfo info;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            auto it = threads_.find(threadId);
            if (it != threads_.end() && it->second->stopped && it->second->info.isException) {
                info  = it->second->info;
                found = true;
            }
        }
        if (!found) {
            sendErrorResponse(req, "exceptionInfo: the thread is not stopped on an exception");
            return;
        }
        json details = {
            {"typeName", info.exceptionClass},
            {"message",  info.exceptionMessage},
        };
        if (!info.stackText.empty())
            details["stackTrace"] = info.stackText;
        sendResponse(req, /*success=*/true, {
            {"exceptionId", info.exceptionClass},
            {"description", info.description},
            {"breakMode",   info.breakMode},
            {"details",     details},
        });
    }

    // --- disconnect ---------------------------------------------------------
    void handleDisconnect(const json& req) {
        sendResponse(req, /*success=*/true, json::object());
        exitCode_ = 0;
        shutdownDebuggee(/*endStuckProgram=*/true);
    }

    // Detach the debugger, release every stopped thread, and join the
    // debuggee. With `endStuckProgram` (a disconnect: the launched program
    // ends with the session) a program still running a few seconds after its
    // threads were released -- an endless loop -- ends with the adapter's
    // process instead of hanging it.
    void shutdownDebuggee(bool endStuckProgram = false) {
        if (shutdownDone_)
            return;
        shutdownDone_ = true;

        if (runtime_) {
            runtime_->debugger().detach();   // run free to completion
            // No thread event, no new stop reaches this server any more.
            runtime_->debugger().setFrontend(nullptr);
        }

        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            shuttingDown_ = true;
            configDone_   = true;            // open the gate if still closed
            // Release every thread parked in onStopped.
            for (auto& [id, r] : threads_) {
                (void)id;
                r->cmd         = DebuggerRuntime::Command::Continue;
                r->resumeReady = true;
                r->cv.notify_all();
            }
        }
        configDoneCv_.notify_all();

        if (runtime_) {
            // Wait (bounded) until the released threads are out of
            // onStopped: they still touch this server's state on the way.
            proto::ProtoContext::UnmanagedScope unmanaged(runtime_->rootCtx());
            std::unique_lock<std::mutex> lk(sessionMu_);
            stopsDrainedCv_.wait_for(lk, std::chrono::seconds(10), [&]{ return inStop_ == 0; });
        }

        if (endStuckProgram && debuggeeThread_ && runtime_) {
            bool done;
            {
                proto::ProtoContext::UnmanagedScope unmanaged(runtime_->rootCtx());
                std::unique_lock<std::mutex> lk(sessionMu_);
                done = debuggeeDoneCv_.wait_for(lk, std::chrono::seconds(3),
                                                [&]{ return debuggeeDone_; });
            }
            if (!done) {
                std::fflush(nullptr);
                std::_Exit(0);
            }
        }

        if (debuggeeThread_ && runtime_) {
            // The join blocks this thread (which owns rootCtx) while the
            // debuggee may still allocate and request a collection.
            proto::ProtoContext::UnmanagedScope unmanaged(runtime_->rootCtx());
            const_cast<proto::ProtoThread*>(debuggeeThread_)->join(runtime_->rootCtx());
        }
        debuggeeThread_ = nullptr;
    }

    // --- outgoing message helpers ------------------------------------------
    void sendResponse(const json& req, bool success, const json& body) {
        json resp = {
            {"seq",         nextSeq()},
            {"type",        "response"},
            {"request_seq", req.value("seq", 0)},
            {"success",     success},
            {"command",     req.value("command", "")},
        };
        if (!body.is_null())
            resp["body"] = body;
        transport_.writeMessage(resp);
    }

    void sendErrorResponse(const json& req, const std::string& message) {
        json resp = {
            {"seq",         nextSeq()},
            {"type",        "response"},
            {"request_seq", req.value("seq", 0)},
            {"success",     false},
            {"command",     req.value("command", "")},
            {"message",     message},
        };
        transport_.writeMessage(resp);
    }

    void sendEvent(const std::string& eventName, const json& body) {
        json ev = {
            {"seq",   nextSeq()},
            {"type",  "event"},
            {"event", eventName},
        };
        if (!body.is_null())
            ev["body"] = body;
        transport_.writeMessage(ev);
    }

    void sendTerminated() {
        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            terminatedSent_ = true;
        }
        sendEvent("terminated", json::object());
    }

    void sendOutput(const std::string& category, const std::string& text) {
        sendEvent("output", { {"category", category}, {"output", text} });
    }

    int nextSeq() { return seq_.fetch_add(1); }

    // --- transport / sequencing --------------------------------------------
    DapStdio         stdio_;                 // before transport_: it owns the streams
    DapTransport     transport_;
    std::atomic<int> seq_{1};
    int              exitCode_{0};

    // --- debug session ------------------------------------------------------
    std::unique_ptr<BytecodeModule> module_;
    std::unique_ptr<STRuntime>      runtime_;
    const proto::ProtoThread*       debuggeeThread_ = nullptr;
    bool                            stopOnEntry_  = false;
    bool                            shutdownDone_ = false;
    unsigned                        exceptionFilters_ = 0;   // until launch
    std::string                     raisedCondition_, uncaughtCondition_;

    // Breakpoints requested by the client, keyed by source path. setBreakpoints
    // replaces a source's list; the whole BreakpointTable is rebuilt from this.
    struct RequestedBreakpoint {
        int                             line = 0;
        bool                            valid = true;   // its hitCondition parsed
        std::shared_ptr<BreakpointSpec> spec;
    };
    std::unordered_map<std::string, std::vector<RequestedBreakpoint>> requestedBreakpoints_;

    // Whether `cond` parses and compiles as a condition (the body of a block,
    // taking `param` when given). Checked when it is set; it is evaluated in
    // the frame each time.
    static bool conditionCompiles(const std::string& cond, const std::string& param,
                                  std::string& error) {
        std::string src = param.empty() ? "[ " + cond + "\n]." : "[ :" + param + " | " + cond + "\n].";
        Parser parser(src);
        auto ast = parser.parseModule();
        if (!parser.errors().empty()) {
            error = "parse error: " + parser.errors().front().message;
            return false;
        }
        Compiler compiler;
        auto bc = compiler.compileModule(*ast);
        if (compiler.hasErrors()) {
            error = "compile error: " + compiler.errors().front();
            return false;
        }
        return true;
    }

    // Shared state between the DAP loop thread and the script threads.
    std::mutex                 sessionMu_;
    std::condition_variable    configDoneCv_;   // configurationDone gate
    std::condition_variable    stopsDrainedCv_; // inStop_ reached 0
    std::condition_variable    debuggeeDoneCv_; // debuggeeDone_ set
    bool                       debuggeeDone_   = false;
    bool                       configDone_     = false;
    bool                       shuttingDown_   = false;
    bool                       terminatedSent_ = false;
    int                        inStop_         = 0;   // threads inside onStopped
    std::map<int, std::unique_ptr<ThreadRec>> threads_;   // by DAP thread id

    // --- F8-4: inspection handle maps --------------------------------------
    // Guarded by sessionMu_. Entries of a thread are dropped when it resumes.
    struct FrameRef {
        int        threadId = 0;
        DebugFrame frame;
    };
    struct VarRef {
        int threadId = 0;
        int frameId  = 0;    // whose locals
    };
    std::unordered_map<int, FrameRef> frameTable_;    // frameId -> frame
    std::unordered_map<int, VarRef>   varRefTable_;   // varRef  -> locals
    int                               nextHandle_ = 1;
};

} // namespace

int runDapServer() {
    DapServer server;
    return server.run();
}

} // namespace protoST
