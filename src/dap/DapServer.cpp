#include "dap/DapServer.h"
#include "dap/DapTransport.h"

#include <atomic>
#include <iostream>
#include <string>

namespace protoST {

namespace {

using nlohmann::json;

// The DAP server skeleton (F8-2).
//
// This task implements only the transport plumbing and message loop:
//   * `initialize`  -> capabilities response + `initialized` event
//   * `disconnect`  -> response, then exit the loop
//   * any other command -> a benign success-with-empty-body stub
//
// F8-3 / F8-4 fill in the real handlers. See `handleRequest` below: each
// command is dispatched there; adding a handler is a new `if` branch (or a
// new method called from one). The default branch already keeps VS Code from
// hanging, so partial wiring stays well-behaved.
class DapServer {
public:
    DapServer() : transport_(std::cin, std::cout) {}

    int run() {
        for (;;) {
            auto msg = transport_.readMessage();
            if (!msg)
                break;                       // EOF / stream error / malformed
            const json& m = *msg;
            std::string type = m.value("type", "");
            if (type != "request") {
                // The adapter only ever receives requests from the client.
                std::cerr << "[dap] ignoring non-request message of type '"
                          << type << "'\n";
                continue;
            }
            if (handleRequest(m))
                break;                       // handler asked to stop the loop
        }
        return exitCode_;
    }

private:
    // Returns true when the message loop should terminate.
    bool handleRequest(const json& req) {
        std::string command = req.value("command", "");

        // ---- F8-2: lifecycle skeleton -------------------------------------
        if (command == "initialize") {
            handleInitialize(req);
            return false;
        }
        if (command == "disconnect") {
            sendResponse(req, /*success=*/true, json::object());
            exitCode_ = 0;
            return true;                     // stop the loop
        }

        // ---- F8-3 hook: launch, setBreakpoints, configurationDone,
        //      continue, next, stepIn, stepOut, threads ----------------------
        // ---- F8-4 hook: stackTrace, scopes, variables, evaluate -----------
        // Add real handlers above this point. Until then, every other command
        // gets a benign success stub so the protocol stays well-behaved.
        std::cerr << "[dap] unhandled command: " << command << '\n';
        sendResponse(req, /*success=*/true, json::object());
        return false;
    }

    void handleInitialize(const json& req) {
        // Minimal capabilities. Later tasks extend this object as features
        // (breakpoints, stepping, evaluation) come online.
        json caps = {
            {"supportsConfigurationDoneRequest", true},
        };
        sendResponse(req, /*success=*/true, caps);
        // DAP requires the adapter to emit `initialized` once it is ready to
        // accept configuration requests (setBreakpoints, configurationDone).
        sendEvent("initialized", json::object());
    }

    // --- outgoing message helpers -----------------------------------------
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

    int nextSeq() { return seq_.fetch_add(1); }

    DapTransport     transport_;
    std::atomic<int> seq_{1};   // monotonically increasing seq for our messages
    int              exitCode_{0};
};

} // namespace

int runDapServer() {
    DapServer server;
    return server.run();
}

} // namespace protoST
