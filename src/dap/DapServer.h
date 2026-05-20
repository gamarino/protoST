#pragma once

namespace protoST {

// Runs the DAP (Debug Adapter Protocol) server over stdin/stdout.
// VS Code launches `protost --dap` as a subprocess and speaks DAP over the
// adapter's stdin/stdout. Returns a process exit code (0 on clean disconnect).
int runDapServer();

} // namespace protoST
