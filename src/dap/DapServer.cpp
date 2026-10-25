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
#include <algorithm>
#include <deque>
#include <filesystem>
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

// --- paths ------------------------------------------------------------------
//
// "Client" paths are the debugger UI's (VS Code on Windows, say); "adapter"
// paths are where protost runs (a Linux host over ssh). launch.pathMappings
// relates the two; without it they are the same.

std::string forwardSlashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

// A Windows-looking path: a drive letter, or a UNC share.
bool looksWindows(const std::string& p) {
    return (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
        || p.rfind("\\\\", 0) == 0;
}

std::string lowered(std::string p) {
    for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}

// The part of `path` below `root` ("" for the root itself, else starting with
// '/'), or nullopt when `path` is not under it. Separators are compared as
// '/'; case-insensitively when `caseless`.
std::optional<std::string> underRoot(const std::string& path, const std::string& root, bool caseless) {
    std::string p = forwardSlashes(path), r = forwardSlashes(root);
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    if (p.size() < r.size()) return std::nullopt;
    const std::string head = p.substr(0, r.size());
    if (caseless ? lowered(head) != lowered(r) : head != r) return std::nullopt;
    if (p.size() == r.size()) return std::string();
    if (r.back() == '/') return "/" + p.substr(r.size());
    if (p[r.size()] != '/') return std::nullopt;
    return p.substr(r.size());
}

// `root` + `rest` (rest from underRoot), with the root's separator style.
std::string joinRoot(const std::string& root, const std::string& rest) {
    std::string r = root;
    while (r.size() > 1 && (r.back() == '/' || r.back() == '\\')) r.pop_back();
    const bool backslash = looksWindows(root) && root.find('/') == std::string::npos;
    std::string tail = rest;
    if (backslash) for (char& c : tail) if (c == '/') c = '\\';
    return r + tail;
}

// The identity of an adapter-side file, for comparing two spellings of it.
std::string fileKey(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty() || path[0] == '<') return path;
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(fs::path(path), ec);
    std::string k = ec ? forwardSlashes(path) : canon.generic_string();
#ifdef _WIN32
    k = lowered(k);
#endif
    return k;
}

std::string basenameOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

bool readWholeFile(const std::string& path, std::string& out) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    char chunk[65536];
    size_t got;
    out.clear();
    while ((got = std::fread(chunk, 1, sizeof chunk, fp)) > 0) out.append(chunk, got);
    std::fclose(fp);
    return true;
}

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
    // Breakpoints as the client requested them, and the path mappings.
    struct RequestedBreakpoint {
        int                             id = 0;
        int                             line = 0;
        bool                            valid = true;   // its hitCondition parsed
        bool                            verified = false;
        std::string                     message;
        std::shared_ptr<BreakpointSpec> spec;
    };
    struct RequestedSource {
        std::string                      adapterPath;
        json                             clientSource;   // echoed back
        std::vector<RequestedBreakpoint> bps;
    };
    struct PathMapping {
        std::string localRoot, remoteRoot;
        // remoteRoot with its symbolic links resolved, when it exists here
        // (macOS: /var/folders is /private/var/folders); "" otherwise.
        std::string remoteCanonical;
    };
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
        if (command == "source")        { handleSource(req);        return false; }
        if (command == "loadedSources") { handleLoadedSources(req); return false; }

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
            {"supportsLoadedSourcesRequest",     true},
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
        bool stopOnEntry    = args.value("stopOnEntry", false);

        // pathMappings: [{localRoot, remoteRoot}] -- "local" is the client's
        // side, "remote" this adapter's.
        if (args.contains("pathMappings")) {
            const json& pm = args["pathMappings"];
            if (!pm.is_array()) {
                sendErrorResponse(req, "launch: 'pathMappings' must be an array of {localRoot, remoteRoot}");
                return;
            }
            for (const auto& m : pm) {
                const std::string local  = m.is_object() ? m.value("localRoot", "") : "";
                const std::string remote = m.is_object() ? m.value("remoteRoot", "") : "";
                if (local.empty() || remote.empty()) {
                    sendErrorResponse(req, "launch: every pathMappings entry needs a non-empty "
                                           "'localRoot' (the client's path) and 'remoteRoot' "
                                           "(the path where protost runs)");
                    return;
                }
                std::error_code ec;
                std::string canon;
                auto c = std::filesystem::weakly_canonical(std::filesystem::path(remote), ec);
                if (!ec) canon = c.generic_string();
                mappings_.push_back({local, remote, canon == forwardSlashes(remote) ? std::string() : canon});
            }
        }

        std::string program = toAdapterPath(args.value("program", ""));
        if (program.empty()) {
            sendErrorResponse(req, "launch: missing 'program'");
            return;
        }
        // cwd: the directory `Import from:` resolves relative module names in.
        const std::string cwd = toAdapterPath(args.value("cwd", ""));
        if (!cwd.empty()) {
            std::error_code ec;
            std::filesystem::current_path(std::filesystem::path(cwd), ec);
            if (ec) {
                sendErrorResponse(req, "launch: cannot change to cwd '" + cwd + "': " + ec.message());
                return;
            }
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

        // The modules known so far: the program and the kernel the runtime
        // loaded on construction. Imports are added as they load
        // (onModuleLoaded).
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            modules_.push_back(module_.get());
            for (const BytecodeModule* m : runtime_->loadedModules()) modules_.push_back(m);
            stdlibKey_ = fileKey(runtime_->stdlibDirectory());
        }
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
    // The source is named by its client path (translated by pathMappings) or
    // by a sourceReference this adapter handed out. Its breakpoints resolve in
    // every loaded module compiled from that file -- the program, a kernel
    // file, an import -- and those of a module not loaded yet are armed when
    // it loads (a `breakpoint` changed event then marks them verified).
    void handleSetBreakpoints(const json& req) {
        const json& args = req.value("arguments", json::object());
        const json source = args.value("source", json::object());
        std::string adapterPath;
        const int ref = source.value("sourceReference", 0);
        if (ref > 0) {
            std::lock_guard<std::mutex> lk(bpMu_);
            auto it = refPath_.find(ref);
            if (it != refPath_.end()) adapterPath = it->second;
        } else {
            adapterPath = toAdapterPath(source.value("path", ""));
        }

        // Each breakpoint's condition / hitCondition / logMessage; one whose
        // hitCondition does not parse is reported unverified and not armed.
        RequestedSource rs;
        rs.adapterPath  = adapterPath;
        rs.clientSource = source;
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
                r.spec    = spec;
                r.message = problem;
                rs.bps.push_back(r);
                problems.push_back(problem);
            }
        }

        json result = json::array();
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            for (auto& bp : rs.bps) bp.id = nextBreakpointId_++;
            // setBreakpoints REPLACES all breakpoints for this source.
            const std::string key = fileKey(adapterPath);
            requested_[key] = rs;
            // Before launch (VS Code may send setBreakpoints first) nothing
            // resolves yet: unverified for now, armed in launch.
            rearmLocked();
            for (const auto& bp : requested_[key].bps)
                result.push_back(breakpointJson(requested_[key], bp));
        }
        sendResponse(req, /*success=*/true, { {"breakpoints", result} });
    }

    // Requires bpMu_. The DAP form of a breakpoint.
    json breakpointJson(const RequestedSource& rs, const RequestedBreakpoint& bp) const {
        json r = { {"id", bp.id}, {"verified", bp.verified}, {"line", bp.line},
                   {"source", rs.clientSource} };
        if (!bp.message.empty()) r["message"] = bp.message;
        return r;
    }

    // Rebuilds the BreakpointTable from every requested source's lines, in
    // one step: running threads never see it half built.
    void rearmAllBreakpoints() {
        std::lock_guard<std::mutex> lk(bpMu_);
        rearmLocked();
    }

    // Requires bpMu_. Answers the breakpoints whose `verified` changed.
    std::vector<std::pair<std::string, int>> rearmLocked() {
        std::vector<std::pair<std::string, int>> changed;   // (source key, index)
        std::vector<BreakpointTable::Location> entries;
        for (auto& [key, rs] : requested_) {
            for (size_t k = 0; k < rs.bps.size(); ++k) {
                RequestedBreakpoint& bp = rs.bps[k];
                bool verified = false;
                if (bp.valid) {
                    for (const BytecodeModule* top : modules_) {
                        if (moduleKey(top) != key) continue;
                        size_t pc = SIZE_MAX;
                        if (const BytecodeModule* m = searchModuleForLine(top, bp.line, pc)) {
                            entries.push_back({m, pc, bp.spec});
                            verified = true;
                        }
                    }
                }
                if (verified != bp.verified) changed.emplace_back(key, static_cast<int>(k));
                bp.verified = verified;
            }
        }
        if (runtime_)
            runtime_->debugger().breakpoints().assign(entries);
        return changed;
    }

    // Requires bpMu_. The file identity of a loaded module (cached).
    const std::string& moduleKey(const BytecodeModule* m) {
        auto it = moduleKeys_.find(m);
        if (it == moduleKeys_.end())
            it = moduleKeys_.emplace(m, fileKey(m->sourceName())).first;
        return it->second;
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

    // --- loaded modules -----------------------------------------------------
    // On the loading thread, before the module's top level runs: list it,
    // arm its breakpoints, tell the client.
    void onModuleLoaded(const BytecodeModule* m) override {
        json source;
        std::vector<json> changedBps;
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            modules_.push_back(m);
            for (const auto& [key, k] : rearmLocked()) {
                const RequestedSource& rs = requested_[key];
                changedBps.push_back(breakpointJson(rs, rs.bps[static_cast<size_t>(k)]));
            }
            source = sourceJsonLocked(m->sourceName());
        }
        sendEvent("loadedSource", { {"reason", "new"}, {"source", source} });
        for (const json& b : changedBps)
            sendEvent("breakpoint", { {"reason", "changed"}, {"breakpoint", b} });
    }

    // --- sources ------------------------------------------------------------
    //
    // A source the client can open itself goes by `path` (the client's path
    // under pathMappings). Anything else goes by a `sourceReference` whose
    // text the `source` request serves: a file under no mapping (the kernel
    // and stdlib of a remote protost, typically), and code that has no file
    // (a "<...>" source name). Kernel and stdlib files are deemphasized.

    // client -> adapter
    std::string toAdapterPath(const std::string& clientPath) const {
        if (mappings_.empty() || clientPath.empty()) return clientPath;
        const PathMapping* best = nullptr;
        std::string bestRest;
        for (const auto& m : mappings_) {
            auto rest = underRoot(clientPath, m.localRoot, looksWindows(m.localRoot));
            if (rest && (!best || m.localRoot.size() > best->localRoot.size())) {
                best = &m;
                bestRest = *rest;
            }
        }
        return best ? joinRoot(best->remoteRoot, bestRest) : clientPath;
    }

    // adapter -> client; nullopt when no mapping covers it.
    std::optional<std::string> toClientPath(const std::string& adapterPath) const {
        const PathMapping* best = nullptr;
        std::string bestRest;
        // The path as spelled, and with its symbolic links resolved: a module
        // found through the working directory carries the resolved spelling.
        std::string canonical;
        {
            std::error_code ec;
            auto c = std::filesystem::weakly_canonical(std::filesystem::path(adapterPath), ec);
            if (!ec) canonical = c.generic_string();
        }
        for (const auto& m : mappings_) {
            for (const std::string* root : std::initializer_list<const std::string*>{&m.remoteRoot, &m.remoteCanonical}) {
                if (root->empty()) continue;
                for (const std::string* path : std::initializer_list<const std::string*>{&adapterPath, &canonical}) {
                    if (path->empty()) continue;
                    auto rest = underRoot(*path, *root, looksWindows(*root));
                    if (rest && (!best || m.remoteRoot.size() > best->remoteRoot.size())) {
                        best = &m;
                        bestRest = *rest;
                    }
                }
            }
        }
        if (!best) return std::nullopt;
        return joinRoot(best->localRoot, bestRest);
    }

    // Requires bpMu_.
    int sourceRefLocked(const std::string& adapterPath) {
        auto it = pathRef_.find(adapterPath);
        if (it != pathRef_.end()) return it->second;
        const int ref = nextSourceRef_++;
        pathRef_[adapterPath] = ref;
        refPath_[ref] = adapterPath;
        return ref;
    }

    // Requires bpMu_. The DAP `source` of code compiled from `adapterPath`.
    json sourceJsonLocked(const std::string& adapterPath) {
        const bool noFile = adapterPath.empty() || adapterPath[0] == '<';
        const bool library = !noFile && !stdlibKey_.empty() &&
            underRoot(fileKey(adapterPath), stdlibKey_, false).has_value();
        json src;
        if (noFile) {
            src = { {"name", adapterPath.empty() ? std::string("<unknown>") : adapterPath},
                    {"sourceReference", sourceRefLocked(adapterPath)},
                    {"origin", "code without a source file"},
                    {"presentationHint", "deemphasize"} };
            return src;
        }
        if (mappings_.empty()) {
            src = { {"name", basenameOf(adapterPath)}, {"path", adapterPath} };
        } else if (auto client = toClientPath(adapterPath)) {
            src = { {"name", basenameOf(adapterPath)}, {"path", *client} };
        } else {
            // The client cannot open a path on this side: serve the text.
            src = { {"name", basenameOf(adapterPath)},
                    {"sourceReference", sourceRefLocked(adapterPath)},
                    {"origin", adapterPath} };
        }
        if (library) {
            src["presentationHint"] = "deemphasize";
            if (!src.contains("origin")) src["origin"] = "protoST library";
        }
        return src;
    }

    json sourceJson(const std::string& adapterPath) {
        std::lock_guard<std::mutex> lk(bpMu_);
        return sourceJsonLocked(adapterPath);
    }

    // `source`: the text of a sourceReference, or of a path the adapter can
    // read (a client that cannot see this file system asks this way).
    void handleSource(const json& req) {
        const json& args = req.value("arguments", json::object());
        int ref = args.value("sourceReference", 0);
        const json source = args.value("source", json::object());
        if (ref <= 0) ref = source.value("sourceReference", 0);
        std::string path;
        if (ref > 0) {
            std::lock_guard<std::mutex> lk(bpMu_);
            auto it = refPath_.find(ref);
            if (it == refPath_.end()) {
                sendErrorResponse(req, "source: unknown sourceReference " + std::to_string(ref));
                return;
            }
            path = it->second;
        } else {
            path = toAdapterPath(source.value("path", ""));
        }
        std::string content;
        if (path.empty() || path[0] == '<' || !readWholeFile(path, content)) {
            sendErrorResponse(req, "source: the text of '" + path + "' is not available");
            return;
        }
        sendResponse(req, /*success=*/true, { {"content", content}, {"mimeType", "text/x-smalltalk"} });
    }

    // `loadedSources`: the program, the kernel files and the imports.
    void handleLoadedSources(const json& req) {
        json list = json::array();
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            std::vector<std::string> seen;
            for (const BytecodeModule* m : modules_) {
                const std::string& key = moduleKey(m);
                if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
                seen.push_back(key);
                list.push_back(sourceJsonLocked(m->sourceName()));
            }
        }
        sendResponse(req, /*success=*/true, { {"sources", list} });
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
            std::string name = (mod && !mod->debugName().empty())
                ? mod->debugName()
                : ("frame " + std::to_string(i));

            json frame = {
                {"id",     frameId},
                {"name",   name},
                {"line",   df.line},
                {"column", 1},
            };
            if (mod) {
                frame["source"] = sourceJson(mod->sourceName());
                if (frame["source"].contains("presentationHint"))
                    frame["presentationHint"] = "subtle";
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
    std::vector<PathMapping> mappings_;     // set by launch, then read-only

    // Guarded by bpMu_ (the adapter thread, and a script thread that loads a
    // module).
    std::mutex                                          bpMu_;
    std::map<std::string, RequestedSource>              requested_;    // by fileKey
    std::vector<const BytecodeModule*>                  modules_;      // loaded, top level
    std::unordered_map<const BytecodeModule*, std::string> moduleKeys_;
    std::string                                         stdlibKey_;
    std::unordered_map<std::string, int>                pathRef_;      // adapter path -> ref
    std::map<int, std::string>                          refPath_;
    int                                                 nextSourceRef_ = 1;
    int                                                 nextBreakpointId_ = 1;

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
