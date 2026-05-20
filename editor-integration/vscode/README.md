# protoST Debugger — VS Code extension

A minimal VS Code extension that lets you debug protoST (`.st`) scripts using
the protoST DAP debug adapter. The adapter is the `protost` binary itself,
invoked as `protost --dap`, which speaks the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
over stdin/stdout.

This extension is intentionally minimal: it only registers the `protost` debug
`type` and tells VS Code how to spawn the adapter. There is no TypeScript build
step, no bundler, and no marketplace packaging.

## Requirements

- `protost` must be built (see `docs/debugging.md` in the repository root).
- `protost` must be on your `PATH`, or set its absolute path in the
  `protost.path` setting (Settings → Extensions → protoST).

## Install

The extension is a plain folder — install it by copying or symlinking it into
your VS Code extensions directory:

```bash
# Symlink (recommended — picks up updates automatically)
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protost-debug-0.1.0

# ...or copy
cp -r editor-integration/vscode ~/.vscode/extensions/protost-debug-0.1.0
```

Then reload VS Code (`Developer: Reload Window`).

Alternatively, package it with `vsce` and install the `.vsix`:

```bash
npm install -g @vscode/vsce
cd editor-integration/vscode && vsce package
code --install-extension protost-debug-0.1.0.vsix
```

## Use

1. Open a folder containing `.st` scripts.
2. Open a `.st` file and click in the gutter to set breakpoints.
3. Open the Run and Debug panel, choose **Add Configuration…** → **protoST**,
   or use the ready-made `examples/.vscode/launch.json`.
4. Press F5.

A launch configuration looks like:

```json
{
  "type": "protost",
  "request": "launch",
  "name": "Debug current protoST script",
  "program": "${workspaceFolder}/examples/pump_twin.st",
  "stopOnEntry": false
}
```

| Attribute     | Type    | Required | Meaning                                            |
|---------------|---------|----------|----------------------------------------------------|
| `program`     | string  | yes      | Path to the `.st` script to debug.                 |
| `stopOnEntry` | boolean | no       | Stop once before the first statement (default off).|

See `docs/debugging.md` for the full debugging guide and known limitations.
