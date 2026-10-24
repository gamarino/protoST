// Minimal protoST debug adapter glue for VS Code.
//
// The protost binary itself IS the debug adapter: `protost --dap` speaks the
// Debug Adapter Protocol over stdin/stdout. VS Code only needs an extension to
// register the debug `type` and tell it how to spawn that adapter. This factory
// returns a DebugAdapterExecutable that runs `protost --dap`.
//
// `protost` is taken from the `protost.path` setting if set, else from the
// default install location (when present), else from PATH.
//
// Remote launch: a configuration with `remote: {command: [...], path}` runs
// `command... path --dap` instead (ssh, wsl, docker exec ...). The adapter
// does every path translation itself (`pathMappings`); this extension only
// picks the command.
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const os = require("os");

function defaultInstallPath() {
  if (process.platform === "win32") {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
    return path.join(base, "Programs", "proto", "bin", "protost.exe");
  }
  return path.join(os.homedir(), ".local", "bin", "protost");
}

function protostCommand() {
  const configured = vscode.workspace.getConfiguration("protost").get("path");
  if (configured) return configured;
  const installed = defaultInstallPath();
  return fs.existsSync(installed) ? installed : "protost";
}

// Lets F5 on an open .st file work without a launch.json, and fills in
// `program` when a configuration omits it.
const configurationProvider = {
  resolveDebugConfiguration(folder, config) {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "smalltalk") {
        config.type = "protost";
        config.name = "Debug current protoST script";
        config.request = "launch";
        config.program = "${file}";
      }
    }
    if (!config.program) {
      return vscode.window
        .showInformationMessage("protoST: open a .st file or set 'program' in launch.json.")
        .then(() => undefined);
    }
    if (config.remote) {
      const cmd = config.remote.command;
      if (!Array.isArray(cmd) || cmd.length === 0 || cmd.some((a) => typeof a !== "string")) {
        return vscode.window
          .showErrorMessage("protoST: 'remote.command' must be a non-empty array of strings, " +
                            "e.g. [\"ssh\", \"-T\", \"user@host\"] or [\"wsl\", \"-d\", \"Ubuntu-24.04\", \"--\"].")
          .then(() => undefined);
      }
      if (!Array.isArray(config.pathMappings) || config.pathMappings.length === 0) {
        return vscode.window
          .showErrorMessage("protoST: a 'remote' launch needs 'pathMappings': " +
                            "[{\"localRoot\": \"${workspaceFolder}\", \"remoteRoot\": \"<the same folder on the remote side>\"}]. " +
                            "protost runs on the other machine and opens 'program' and the breakpoints' files there.")
          .then(() => undefined);
      }
    }
    return config;
  },
};

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(session) {
      const remote = session.configuration && session.configuration.remote;
      if (remote && Array.isArray(remote.command) && remote.command.length > 0) {
        const remotePath = remote.path || "protost";
        return new vscode.DebugAdapterExecutable(
          remote.command[0], [...remote.command.slice(1), remotePath, "--dap"]);
      }
      return new vscode.DebugAdapterExecutable(protostCommand(), ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("protost", factory),
    vscode.debug.registerDebugConfigurationProvider("protost", configurationProvider)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
