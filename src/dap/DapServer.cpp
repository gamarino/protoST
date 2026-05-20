#include "dap/DapServer.h"
#include "dap/DapTransport.h"

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "debugger/DebuggerRuntime.h"
#include "protoCore.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <iostream>
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
// stop-resume handshake / stepping).
//
// Threading model:
//   * The DAP message loop (`run`) runs on the main thread and NEVER blocks on
//     the debuggee. It must keep reading stdin even while the debuggee is
//     stopped at a breakpoint, otherwise it could not receive `continue`.
//   * `launch` spawns a *debuggee thread*: it waits for the configurationDone
//     gate, then runs `rt.runTopLevel(module)`.
//   * When the engine hits a breakpoint / step / entry it calls the installed
//     DebuggerFrontend (this server). `onStopped` emits a `stopped` event,
//     stores the DebugFrame in shared session state, and blocks the debuggee
//     thread on a condition variable until a resume request arrives.
//   * A resume request handler (`continue`/`next`/`stepIn`/`stepOut`), running
//     on the DAP loop thread, records the pending resume command and signals
//     the cv; the debuggee thread wakes and returns the command to the engine.
//
// MVP scope: a single logical thread (threadId 1). protoST has worker threads
// (F6 v2); a breakpoint hit inside an actor handler runs `onStopped` on a
// worker thread. We serialize stops with `stopMu_` so only one stop is live at
// a time. True multi-thread debugging (separate threadIds, thread list events)
// is beyond this MVP.
class DapServer : public DebuggerFrontend {
public:
    DapServer() : transport_(std::cin, std::cout) {}
    ~DapServer() override { shutdownDebuggee(); }

    int run() {
        for (;;) {
            auto msg = transport_.readMessage();
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
    // Called on the debuggee thread (or a worker thread for an actor stop)
    // when execution halts. Emits a `stopped` event, parks the calling thread
    // on the resume cv, and returns the resume command once it arrives.
    DebuggerRuntime::Command onStopped(STRuntime& /*rt*/, const DebugFrame& frame,
                                       const std::string& reason) override {
        // Serialize concurrent stops (e.g. an actor on a worker thread). MVP:
        // only one stop is processed at a time; a second halting thread waits
        // here until the first resumes.
        std::unique_lock<std::mutex> stopLk(stopMu_);

        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            stoppedFrame_  = frame;
            haveFrame_     = true;
            stopped_       = true;
            resumeReady_   = false;
        }
        sendEvent("stopped", {
            {"reason",            reason},
            {"threadId",          1},
            {"allThreadsStopped", true},
        });

        // Block until a resume request (continue/next/stepIn/stepOut) or
        // disconnect signals us.
        std::unique_lock<std::mutex> lk(sessionMu_);
        resumeCv_.wait(lk, [&]{ return resumeReady_; });
        stopped_   = false;
        haveFrame_ = false;
        return resumeCommand_;
    }

private:
    // ----------------------------------------------------------------------
    // Returns true when the message loop should terminate.
    bool handleRequest(const json& req) {
        std::string command = req.value("command", "");

        // ---- F8-2: lifecycle skeleton -------------------------------------
        if (command == "initialize")       { handleInitialize(req);       return false; }
        if (command == "disconnect")       { handleDisconnect(req);       return true;  }

        // ---- F8-3: launch / breakpoints / session / stepping --------------
        if (command == "launch")           { handleLaunch(req);           return false; }
        if (command == "setBreakpoints")   { handleSetBreakpoints(req);   return false; }
        if (command == "configurationDone"){ handleConfigurationDone(req);return false; }
        if (command == "threads")          { handleThreads(req);          return false; }
        if (command == "continue" || command == "next" ||
            command == "stepIn"   || command == "stepOut") {
            handleResume(req, command);
            return false;
        }

        // ---- F8-4 hook: stackTrace, scopes, variables, evaluate -----------
        std::cerr << "[dap] unhandled command: " << command << '\n';
        sendResponse(req, /*success=*/true, json::object());
        return false;
    }

    void handleInitialize(const json& req) {
        json caps = {
            {"supportsConfigurationDoneRequest", true},
        };
        sendResponse(req, /*success=*/true, caps);
        sendEvent("initialized", json::object());
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
            sendEvent("terminated", json::object());
            return;
        }
        std::fseek(fp, 0, SEEK_END);
        long n = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        std::string src(static_cast<size_t>(n > 0 ? n : 0), '\0');
        if (n > 0) {
            size_t got = std::fread(src.data(), 1, static_cast<size_t>(n), fp);
            src.resize(got);
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
            sendEvent("terminated", json::object());
            return;
        }
        Compiler compiler;
        auto module = compiler.compileModule(*ast);
        if (compiler.hasErrors()) {
            for (const auto& s : compiler.errors())
                sendOutput("stderr", "compile error: " + s + "\n");
            sendErrorResponse(req, "launch: compile error");
            sendEvent("terminated", json::object());
            return;
        }
        module->setSourceName(program);

        // Retain the module + runtime for the session lifetime.
        module_      = std::move(module);
        runtime_     = std::make_unique<STRuntime>();
        stopOnEntry_ = stopOnEntry;
        runtime_->debugger().attach();
        runtime_->debugger().setFrontend(this);

        // Re-resolve any breakpoints requested before the module existed.
        rearmAllBreakpoints();

        // Spawn the debuggee thread.
        debuggeeThread_ = std::thread([this]{ debuggeeMain(); });

        sendResponse(req, /*success=*/true, json::object());
    }

    // The debuggee thread entry point.
    void debuggeeMain() {
        // Gate: wait until `configurationDone` so breakpoints set between
        // `launch` and `configurationDone` are armed before the first
        // instruction runs.
        {
            std::unique_lock<std::mutex> lk(sessionMu_);
            configDoneCv_.wait(lk, [&]{ return configDone_ || shuttingDown_; });
            if (shuttingDown_)
                return;
        }

        // stopOnEntry: stop once with reason "entry" before the first real
        // instruction. We call the frontend directly with a synthetic frame
        // at pc 0 of the top module.
        if (stopOnEntry_ && runtime_->debugger().attached()) {
            DebugFrame entryFrame;
            entryFrame.module = module_.get();
            entryFrame.pc     = 0;
            // onStopped is called directly here (not via enterSession), so we
            // must apply the resulting command/mode ourselves — otherwise a
            // `stepIn` from the entry stop would leave the engine in Free mode.
            DebuggerRuntime::Command cmd = onStopped(*runtime_, entryFrame, "entry");
            auto& dbg = runtime_->debugger();
            dbg.setCommand(cmd);
            switch (cmd) {
                case DebuggerRuntime::Command::Continue:
                    dbg.setMode(DebuggerRuntime::Mode::Free);        break;
                case DebuggerRuntime::Command::Step:
                case DebuggerRuntime::Command::Next:
                    dbg.setMode(DebuggerRuntime::Mode::SingleStep);  break;
                case DebuggerRuntime::Command::Finish:
                    dbg.setMode(DebuggerRuntime::Mode::RunToReturn); break;
            }
        }

        int exitCode = 0;
        try {
            runtime_->runTopLevel(*module_);
        } catch (const std::exception& e) {
            exitCode = 1;
            sendOutput("stderr", std::string("error: ") + e.what() + "\n");
        } catch (...) {
            exitCode = 1;
            sendOutput("stderr", "error: unknown exception\n");
        }

        sendEvent("terminated", json::object());
        sendEvent("exited", { {"exitCode", exitCode} });
    }

    // --- setBreakpoints -----------------------------------------------------
    void handleSetBreakpoints(const json& req) {
        const json& args = req.value("arguments", json::object());
        std::string path = args.value("source", json::object()).value("path", "");

        std::vector<int> lines;
        if (args.contains("breakpoints")) {
            for (const auto& bp : args["breakpoints"])
                lines.push_back(bp.value("line", 0));
        }

        // setBreakpoints REPLACES all breakpoints for this source.
        requestedBreakpoints_[path] = lines;

        // (Re)resolve against the module if it already exists.
        json result = json::array();
        if (module_ && runtime_) {
            // Rebuild the whole breakpoint table from all sources, since
            // BreakpointTable has no per-source clear.
            rearmAllBreakpoints();
            for (int line : lines) {
                size_t pc = SIZE_MAX;
                bool verified = (resolveLineToModule(line, pc) != nullptr);
                result.push_back({ {"verified", verified}, {"line", line} });
            }
        } else {
            // Module not compiled yet (VS Code may send setBreakpoints before
            // launch). Report unverified for now; they are armed in launch.
            for (int line : lines)
                result.push_back({ {"verified", false}, {"line", line} });
        }

        sendResponse(req, /*success=*/true, { {"breakpoints", result} });
    }

    // Rebuilds the BreakpointTable from every requested source's lines.
    void rearmAllBreakpoints() {
        if (!module_ || !runtime_)
            return;
        auto& table = runtime_->debugger().breakpoints();
        table.clear();
        for (const auto& [path, lines] : requestedBreakpoints_) {
            (void)path;
            for (int line : lines) {
                size_t pc = SIZE_MAX;
                const BytecodeModule* m = resolveLineToModule(line, pc);
                if (m)
                    table.add(m, pc);
            }
        }
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
        // MVP: a single logical thread. protoST has worker threads, but the
        // debugger models the top-level script flow as one logical thread.
        sendResponse(req, /*success=*/true, {
            {"threads", json::array({ json{{"id", 1}, {"name", "main"}} })},
        });
    }

    // --- continue / next / stepIn / stepOut --------------------------------
    void handleResume(const json& req, const std::string& command) {
        DebuggerRuntime::Command cmd = DebuggerRuntime::Command::Continue;
        if (command == "continue")      cmd = DebuggerRuntime::Command::Continue;
        else if (command == "next")     cmd = DebuggerRuntime::Command::Next;
        else if (command == "stepIn")   cmd = DebuggerRuntime::Command::Step;
        else if (command == "stepOut")  cmd = DebuggerRuntime::Command::Finish;

        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            resumeCommand_ = cmd;
            resumeReady_   = true;
        }
        resumeCv_.notify_all();

        if (command == "continue")
            sendResponse(req, /*success=*/true, { {"allThreadsContinued", true} });
        else
            sendResponse(req, /*success=*/true, json::object());
    }

    // --- disconnect ---------------------------------------------------------
    void handleDisconnect(const json& req) {
        sendResponse(req, /*success=*/true, json::object());
        exitCode_ = 0;
        shutdownDebuggee();
    }

    // Detach the debugger, release any parked debuggee thread, and join it.
    void shutdownDebuggee() {
        if (shutdownDone_)
            return;
        shutdownDone_ = true;

        if (runtime_)
            runtime_->debugger().detach();   // run free to completion

        {
            std::lock_guard<std::mutex> lk(sessionMu_);
            shuttingDown_ = true;
            configDone_   = true;            // open the gate if still closed
            // If the debuggee is parked in onStopped, release it to continue.
            resumeCommand_ = DebuggerRuntime::Command::Continue;
            resumeReady_   = true;
        }
        configDoneCv_.notify_all();
        resumeCv_.notify_all();

        if (debuggeeThread_.joinable())
            debuggeeThread_.join();
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

    void sendOutput(const std::string& category, const std::string& text) {
        sendEvent("output", { {"category", category}, {"output", text} });
    }

    int nextSeq() { return seq_.fetch_add(1); }

    // --- transport / sequencing --------------------------------------------
    DapTransport     transport_;
    std::atomic<int> seq_{1};
    int              exitCode_{0};

    // --- debug session ------------------------------------------------------
    std::unique_ptr<BytecodeModule> module_;
    std::unique_ptr<STRuntime>      runtime_;
    std::thread                     debuggeeThread_;
    bool                            stopOnEntry_  = false;
    bool                            shutdownDone_ = false;

    // Breakpoints requested by the client, keyed by source path. setBreakpoints
    // replaces a source's list; the whole BreakpointTable is rebuilt from this.
    std::unordered_map<std::string, std::vector<int>> requestedBreakpoints_;

    // Shared state between the DAP loop thread and the debuggee thread.
    std::mutex                 sessionMu_;
    std::condition_variable    configDoneCv_;   // configurationDone gate
    std::condition_variable    resumeCv_;       // resume handshake
    std::mutex                 stopMu_;         // serialize concurrent stops
    bool                       configDone_   = false;
    bool                       shuttingDown_ = false;
    bool                       stopped_      = false;
    bool                       resumeReady_  = false;
    bool                       haveFrame_    = false;
    DebuggerRuntime::Command   resumeCommand_ = DebuggerRuntime::Command::Continue;
    DebugFrame                 stoppedFrame_;   // F8-4 reads this for inspection
};

} // namespace

int runDapServer() {
    DapServer server;
    return server.run();
}

} // namespace protoST
