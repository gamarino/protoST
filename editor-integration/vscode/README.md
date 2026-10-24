# protoST — VS Code extension

A minimal VS Code extension for protoST (`.st`) scripts:

- **Language support** — syntax highlighting (a TextMate grammar), `"…"`
  comments, bracket matching and auto-closing for `[ ]`, `( )`, `{ }`, `' '`.
- **Debugging** — breakpoints, stepping, call stack, variables, the Debug
  Console, conditional / hit-count / log breakpoints, exception breakpoints
  (Raised / Uncaught, in the Breakpoints panel, each with an optional
  condition on the exception `ex`)
  and one thread per actor worker (a thread that stops stops alone), through
  the protoST DAP debug adapter. The adapter is the `protost`
  binary itself, invoked as `protost --dap`, which speaks the
  [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
  over stdin/stdout. What the script prints shows up in the Debug Console.

There is no TypeScript build step and no bundler.

## Requirements

`protost` must be installed or built (see `docs/debugging.md` in the repository
root). The extension looks for it in this order:

1. the `protost.path` setting (Settings → Extensions → protoST), if set;
2. the default install location — `%LOCALAPPDATA%\Programs\proto\bin\protost.exe`
   on Windows, `~/.local/bin/protost` elsewhere — if it exists;
3. `protost` on your `PATH`.

## Install

Package it with `vsce` and install the `.vsix` (works the same on every
platform):

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protost-debug-0.4.0.vsix
```

Or, on Linux and macOS, symlink the folder (picks up edits after a
`Developer: Reload Window`):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protost-debug-0.4.0
```

## Use

1. Open a folder containing `.st` scripts (for example `examples/`).
2. Open a `.st` file and click in the gutter to set breakpoints.
3. Press **F5**. Without a `launch.json` the extension debugs the active
   `.st` file; `examples/.vscode/launch.json` has ready-made configurations,
   and **Add Configuration…** → **protoST** writes one for you.

A launch configuration looks like:

```json
{
  "type": "protost",
  "request": "launch",
  "name": "Debug current protoST script",
  "program": "${file}",
  "stopOnEntry": false
}
```

| Attribute     | Type    | Required | Meaning                                            |
|---------------|---------|----------|----------------------------------------------------|
| `program`     | string  | yes      | Path to the `.st` script to debug.                 |
| `stopOnEntry` | boolean | no       | Stop once before the first statement (default off).|

See `docs/debugging.md` for the full debugging guide and known limitations.
