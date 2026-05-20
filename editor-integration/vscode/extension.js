// Minimal protoST debug adapter glue for VS Code.
//
// The protost binary itself IS the debug adapter: `protost --dap` speaks the
// Debug Adapter Protocol over stdin/stdout. VS Code only needs an extension to
// register the debug `type` and tell it how to spawn that adapter. This factory
// returns a DebugAdapterExecutable that runs `protost --dap`.
//
// `protost` is taken from the `protost.path` setting if set, else from PATH.
const vscode = require("vscode");

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(/* session */) {
      const cfg = vscode.workspace.getConfiguration("protost");
      const command = cfg.get("path") || "protost";
      return new vscode.DebugAdapterExecutable(command, ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("protost", factory)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
