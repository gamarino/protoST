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
code --install-extension protost-debug-0.5.1.vsix
```

Or, on Linux and macOS, symlink the folder (picks up edits after a
`Developer: Reload Window`):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protost-debug-0.5.1
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
| `cwd`         | string  | no       | Working directory (where `Import from:` looks).    |
| `pathMappings`| array   | no       | `[{localRoot, remoteRoot}]`: your paths ↔ protost's. |
| `remote`      | object  | no       | `{command: [...], path}`: run protost elsewhere.   |

## Remote launch

With `remote`, the extension starts `command[0] command[1..] path --dap`
instead of the local `protost --dap` — `["ssh", "-T", "-o", "BatchMode=yes",
"user@host"]`, `["wsl", "-d", "Ubuntu-24.04", "--"]`, `["docker", "exec", "-i",
"ctr"]` — and `pathMappings` (required with `remote`) tells the adapter how your
paths map to the remote ones. **Add Configuration…** offers "protoST: Launch
over SSH" and "protoST: Launch in WSL". The remote shell must not print
anything on a non-interactive start (it would corrupt the protocol), and ssh
must not prompt (use key authentication). The extension is a workspace
extension, so with Remote - SSH / WSL / Dev Containers you can instead install
it on the remote side and debug as if local.

See `docs/debugging.md` for the full debugging guide and known limitations.

## Testing the extension

`test/` holds integration tests that run this extension inside a real VS Code
(an Extension Development Host, through `@vscode/test-electron` and mocha)
and debug `test/fixture/fixture.st` with `protost`, driving VS Code's own
debug API the way F5 does:

```bash
cd editor-integration/vscode
npm install
npm test
```

They check: a local launch (breakpoint hit on the right line, `stackTrace`,
a local's value, the program's stdout as `output` events, `terminated`); F5
without a `launch.json` on the open fixture; the executable lookup (a
`protost.path` that does not exist is refused with a message naming it; with
the setting empty the default install is the process that runs); a `remote`
launch through WSL with `pathMappings` (the breakpoint is set by the Windows
path and the frames come back with Windows paths); a `remote` launch without
`pathMappings` refused with its explanation; the `raised` exception filter;
and that the launch schema, snippets and initial configurations agree.

- **Which VS Code.** `$VSCODE_EXECUTABLE` if set; on Windows the per-user
  install when present (unless it is halfway through an update, which makes
  every new instance refuse to start); otherwise a pinned VS Code is
  downloaded (`$VSCODE_VERSION` overrides the version) into
  `$VSCODE_TEST_CACHE`.
- **Isolation.** A throwaway `--user-data-dir` and `--extensions-dir` under
  `$PROTO_VSCODE_E2E_DIR` (default `C:\tmp\protowin\vscode-e2e` on Windows,
  `$TMPDIR/proto-vscode-e2e` elsewhere), recreated on every run, and every
  other extension disabled: your own VS Code may stay open.
- **Which runtime.** `$PROTO_E2E_EXE` (put into `protost.path`), else the
  extension's own lookup: default install, then `PATH`. The default-install
  test is skipped when there is no default install.
- **Remote.** Runs only on Windows with WSL (`$PROTO_E2E_WSL_DISTRO`, default
  `Ubuntu-24.04`) and the Linux build named in `test/lang.js`; it is skipped
  elsewhere.
- `$PROTO_E2E_GREP` runs only the tests whose title matches.

`test/runTest.js`, `test/suite/` are the same in the five proto* runtimes'
extensions; `test/lang.js` and the fixture hold the per-language part.
`test/` and `node_modules/` are left out of the `.vsix` (`.vscodeignore`).
CI runs the local tests on Linux under `xvfb-run` (`.github/workflows/ci.yml`).
