# Debugging protoST scripts in VS Code

protoST ships a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
(DAP) adapter built into the `protost` binary. This lets you debug `.st`
scripts from VS Code with breakpoints, stepping, a call stack, a variables
panel, and a watch/evaluate console.

## What the DAP adapter is

The Debug Adapter Protocol is the JSON-over-stdio protocol VS Code (and other
editors) use to talk to debuggers. Running:

```bash
protost --dap
```

starts protoST as a DAP adapter: it reads `Content-Length`-framed DAP messages
from stdin and writes them to stdout. It implements the requests `initialize`,
`launch`, `setBreakpoints`, `configurationDone`, `continue`, `next`, `stepIn`,
`stepOut`, `threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, and
`disconnect`, and emits the events `initialized`, `stopped`, `terminated`,
`exited`, and `output`.

You do not normally run `protost --dap` by hand — VS Code spawns it for you via
the extension below.

## Building `protost`

The runtime depends on [protoCore](../../protoCore), which must be built first.

```bash
cd protoST
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

This produces `build/protost`. Put it on your `PATH` (or note its absolute
path for the extension setting below):

```bash
export PATH="$PWD:$PATH"   # from inside build/
protost --version
```

## Installing the VS Code extension

VS Code cannot launch an arbitrary debug adapter without an extension that
registers the debug `type`. protoST ships a minimal extension (no TypeScript,
no build step) under `editor-integration/vscode/`.

Install it by symlinking it into your VS Code extensions directory:

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protost-debug-0.1.0
```

Then run `Developer: Reload Window` in VS Code. See
`editor-integration/vscode/README.md` for the copy / `.vsix` alternatives.

If `protost` is not on your `PATH`, set its absolute path in VS Code Settings
under **Extensions → protoST → Protost: Path**.

## Setting up `launch.json` and starting a session

A ready-to-use configuration lives at `examples/.vscode/launch.json`. To create
your own, open the Run and Debug panel, click **Add Configuration…**, and
choose **protoST**. A launch configuration looks like:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "protost",
      "request": "launch",
      "name": "Debug current protoST script",
      "program": "${workspaceFolder}/examples/pump_twin.st",
      "stopOnEntry": false
    }
  ]
}
```

The `launch` request accepts exactly two attributes (confirmed against
`src/dap/DapServer.cpp`):

| Attribute     | Type    | Required | Meaning                                              |
|---------------|---------|----------|------------------------------------------------------|
| `program`     | string  | yes      | Path to the `.st` script to run under the debugger.  |
| `stopOnEntry` | boolean | no       | If true, stop once with reason `entry` before the first statement. Default `false`. |

To start a session:

1. Open a `.st` file and click in the editor gutter to set breakpoints.
2. Select the configuration in the Run and Debug panel.
3. Press **F5**.

Execution runs until it hits a breakpoint (or the entry point, if
`stopOnEntry` is set), then VS Code shows the stopped state.

## What works

- **Breakpoints by line** — set/clear in the gutter of `.st` files; the adapter
  resolves them via the bytecode pc-to-source-line map (F8-1).
- **Stepping** — step in, step over, step out, and continue.
- **Call stack** — the `stackTrace` request returns named frames with source
  file and line.
- **Variables** — the variables panel shows locals and arguments with their
  real names and current values.
- **Watch / evaluate** — the `evaluate` request powers the Watch panel and the
  Debug Console; it evaluates expressions in the selected frame's scope
  (arithmetic and in-scope variables work).
- **Debug Console** — adapter `output` events are surfaced; you can type
  expressions to evaluate.

## Known limitations (MVP)

This is a minimum-viable adapter. Honest limitations as of F8:

- **Single logical thread.** The adapter reports one thread (`threadId 1`).
  Debugging concurrent multi-actor-thread execution is beyond the MVP — actor
  worker threads are not individually surfaced.
- **`next` is aliased to `step`.** There is no frame-depth-aware step-over yet;
  `next` currently behaves like `stepIn`.
- **Call stacks are usually a single frame in practice.** Multi-level stacks
  display correctly when produced, but today they are only produced by
  same-engine user-method `SEND`s. Block bodies run in nested engines (an F6
  v3 A scope limitation) and class-side methods hit a separate pre-existing
  `unimplemented opcode` bug — so most stops show a single frame.
- **Flat variables panel.** Objects are shown as `<obj>` and are not
  expandable; only scalar locals/arguments render their value.

These are scope boundaries of the MVP adapter, not regressions — the underlying
DAP exchange is verified by the `cli_dap_session` integration test.

## Verifying the adapter without VS Code

You can drive a full session from a script. The integration test
`tests/cli/test_cli_dap_session.sh` does exactly this: `initialize` → `launch`
→ `setBreakpoints` → `configurationDone` → `stopped` → `stackTrace` → `scopes`
→ `variables` → `evaluate` → `continue` → `terminated`. Run it with:

```bash
bash tests/cli/test_cli_dap_session.sh ./build/protost
```
