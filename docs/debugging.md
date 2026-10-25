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
`launch`, `setBreakpoints` (with `condition`, `hitCondition` and
`logMessage`), `setExceptionBreakpoints` (with conditional `filterOptions`), `source`,
`loadedSources`, `configurationDone`,
`continue`, `next`, `stepIn`, `stepOut`, `pause`, `threads`, `stackTrace`,
`scopes`, `variables`, `evaluate`, `exceptionInfo`, `disconnect` and
`terminate`, and emits the events `initialized`, `stopped`, `thread`, `breakpoint`, `loadedSource`,
`terminated`, `exited`, and `output`.

You do not normally run `protost --dap` by hand — VS Code spawns it for you via
the extension below.

## Building `protost`

The runtime depends on [protoCore](https://github.com/numaes/protoCore), which must be built first.

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
no build step) under `editor-integration/vscode/`; it also gives `.st` files
syntax highlighting. Package and install it:

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protost-debug-0.5.1.vsix
```

See `editor-integration/vscode/README.md` for the symlink alternative.

The extension uses the `protost` named in VS Code Settings under
**Extensions → protoST → Protost: Path**; if that is empty, the one in the
default install location (`%LOCALAPPDATA%\Programs\proto\bin\protost.exe` on
Windows, `~/.local/bin/protost` elsewhere); else `protost` from your `PATH`.

## Setting up `launch.json` and starting a session

Without a `launch.json`, **F5** on an open `.st` file debugs that file. Ready-to-use
configurations live in `examples/.vscode/launch.json` (open the `examples/`
folder in VS Code to pick them up). To create
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

The `launch` request accepts these attributes (confirmed against
`src/dap/DapServer.cpp`; the extension adds `remote`, see Remote debugging):

| Attribute     | Type    | Required | Meaning                                              |
|---------------|---------|----------|------------------------------------------------------|
| `program`     | string  | yes      | Path to the `.st` script to run under the debugger.  |
| `stopOnEntry` | boolean | no       | If true, stop once with reason `entry` before the first statement. Default `false`. |
| `cwd`         | string  | no       | Working directory: where `Import from:` finds modules. |
| `pathMappings`| array   | no       | `[{localRoot, remoteRoot}]` for a remote session (see Remote debugging). |

To start a session:

1. Open a `.st` file and click in the editor gutter to set breakpoints.
2. Select the configuration in the Run and Debug panel.
3. Press **F5**.

Execution runs until it hits a breakpoint (or the entry point, if
`stopOnEntry` is set), then VS Code shows the stopped state.

## What works

- **Breakpoints by line** — set/clear in the gutter of `.st` files; the adapter
  resolves them via the bytecode pc-to-source-line map (F8-1). A breakpoint on
  a line inside a method or a multi-line block stops there, not where the
  method or the block is defined.
- **Stepping** — by source line and call depth, per thread:
  - **Step Over** (`next`) runs to the next line of the same frame (or of its
    caller, when the frame returns). A method call, and a block a primitive
    runs (`do:`, `collect:`, `inject:into:` …), are stepped over whole.
  - **Step Into** (`stepIn`) stops at the next line, or at the first line of a
    method or a block the current line calls.
  - **Step Out** (`stepOut`) runs until the current frame returns and stops in
    the caller.
  - A breakpoint met on the way stops first.
- **Call stack** — the `stackTrace` request returns every frame of the
  stopped thread, innermost first, with source file and line: methods, and
  the blocks that primitives run in engines of their own (`[] in Calc>>sumOf:`
  above `Calc>>sumOf:` above `<module>`). A caller frame shows the line of the
  send in progress.
- **Variables** — the Locals scope shows `self`, the arguments and the
  temporaries of the selected frame with their real names and printed values.
  A temporary shared with a block shows its current value, and a block frame
  also shows the enclosing temporaries it uses. Compiler temporaries are not
  shown. Values are flat (objects print as `a Counter` and are not
  expandable).
- **Watch / evaluate** — the `evaluate` request powers the Watch panel and the
  Debug Console. The expression runs on the stopped thread, in the selected
  frame: its variables are in scope by value (`doubled + n`, `box at: 1`; an
  assignment to one does not reach the frame). Breakpoints and exception
  filters do not stop an evaluation.
- **Exception breakpoints** — the Breakpoints panel lists two filters:
  - **Raised Exceptions** (`raised`, off by default) stops when any exception
    is signalled — `signal`, `signal:`, an error a primitive raises (an index
    out of bounds, a missing key), protoCore's own errors (`ZeroDivide`), a
    `doesNotUnderstand:` — before any handler runs, so the stack, the locals
    and `evaluate` are exactly as at the signal.
  - **Uncaught Exceptions** (`uncaught`, on by default) stops only when no
    `on:do:` handler on the thread will take the exception and its default
    action ends the activation (an `Error`, or any non-resumable exception).
    Smalltalk looks for the handler before it unwinds anything, so this stop
    too has the stack intact. An unhandled `Warning` resumes with nil and does
    not count.

  The stop's reason is `exception`; its description, and the `exceptionInfo`
  request (the exception widget), give the class and the message text, the
  break mode (`always` for raised, `unhandled` for uncaught) and the stack as
  text. An error raised inside a primitive stops at the frame that sent the
  message, and the description says so ("raised by a primitive"). An error
  raised inside kernel Smalltalk code (`1 / 0` signals from
  `Fraction>>numerator:denominator:`) stops in that kernel method, with the
  caller frames below it.

  What never stops: protoST's internal control flow, which is C++ unwinding and
  not a signal — the non-local return `^` from a block (`NonLocalReturn`), the
  unwinding to an `on:do:` after its handler (`UnwindToHandler`), `resume:`
  (`ResumeSignal`), `retry` (`RetrySignal`), `pass` / `outer` (`PassSignal`),
  the nesting-limit unwinding (`NestingLimitUnwind`), an actor's cooperative
  yield in `wait` (`FutureYield`), `halt` (`DebuggerHalt`, which has its own
  stop) and Ctrl-C in the REPL (`InterruptSignal`). An exception that already
  went through every handler and continues outward (`UnhandledSTException`) is
  not reported a second time. `pass` / `outer` and a handler re-entered after
  the nesting limit are the same signal, not a new one.
- **Threads** — every thread that runs script code is a thread in VS Code's
  Call Stack view:
  - `main` (thread 1) runs the script. While it waits on a Future it may run
    actor messages itself; a stop then shows the actor in its name.
  - `worker N` — the actor worker pool. A worker stopped inside an actor's
    method is named after the actor: `worker 2: actor Counter#1` (the actor's
    class and a number the debugger gives each actor it sees).

  A thread appears (a `thread` started event) the first time it runs an
  instruction, and disappears when it ends. Workers that never ran script
  code are not listed.
- **One-thread stop** — a thread that stops (breakpoint, step, pause,
  exception) stops alone: the other threads keep running, and several threads
  can be stopped at the same time, each inspected on its own. Continue, the
  steps and Pause act on the thread they are sent for. A breakpoint applies to
  every thread; stepping one thread never makes another stop. Collections go on
  while threads are stopped: a stopped thread waits outside protoCore's
  stop-the-world quorum, and its frames stay roots.
- **Pause** — pauses the selected thread at its next instruction. A thread
  blocked in native code (waiting on a Future, on I/O, on an idle worker's
  queue) pauses when it is back in the interpreter: the request answers at
  once, the stop arrives then.
- **Debug Console** — what the script prints (stdout and stderr) appears
  here as adapter `output` events; you can type expressions to evaluate. The
  adapter's own stdout is the protocol channel, so the debuggee's stdout and
  stderr are captured and forwarded, and its stdin reads end of file (a script
  that reads stdin sees no input under the debugger).

## Conditions, hit counts and logpoints

Every expression below is Smalltalk, evaluated the way the Debug Console
evaluates: on the thread that reached the breakpoint, in its frame (the frame's
variables in scope by value, globals and classes as usual). While it runs the
debugger stops nowhere on that thread -- a breakpoint in a method the
expression calls, an exception it raises, a step in progress -- and the
program's own `on:do:` handlers are hidden from it, so an error in the
expression never unwinds the program.

**Exception filter conditions.** Each filter in the Breakpoints panel takes a
condition (right-click → Edit Condition). The exception is bound as `ex`
(shadowing a frame variable named `ex`):

```smalltalk
ex isKindOf: ZeroDivide
ex messageText = 'too big 4'
(ex isKindOf: SubscriptOutOfBounds) and: [ index > 10 ]
```

The filter stops only when the condition answers `true`.

**Line breakpoints** (right-click a breakpoint → Edit Breakpoint):

- **Condition** — stops when the expression answers `true` (`i = 4`,
  `total > 100`).
- **Hit count** — counts the hits whose condition passed, and stops on:

  | Hit count | Stops on                    |
  |-----------|-----------------------------|
  | `3` or `== 3` | the 3rd hit only        |
  | `>= 5`    | the 5th hit and every later one |
  | `> 5`     | the 6th and later           |
  | `< 3`     | the 1st and 2nd             |
  | `<= 3`    | the 1st to the 3rd          |
  | `% 2`     | every 2nd (2nd, 4th, …)     |

  Anything else is rejected: the breakpoint shows as unverified, with the
  reason, and does not stop. Hits are counted from the last time the file's
  breakpoints were set.
- **Log message** — a logpoint does not stop: it writes the message to the
  Debug Console, with each `{expression}` replaced by its value in the frame (a
  string by its characters, anything else as it prints); `{{` and `}}` are
  literal braces. An expression that fails reads `<error: message>`. With a
  condition or a hit count it logs only when they pass.

**When a condition fails** — a syntax error, an exception, or a result that is
not a Boolean (`nil`, a number) — the breakpoint (or the filter) STOPS, and
says why: the stop's description reads ``breakpoint condition `i fooBar`
failed: doesNotUnderstand: fooBar …`` (``exception breakpoint condition …`` for
a filter), and the same line goes to the Debug Console. A condition that does
not compile is also reported when it is set.

Limitation: the expression cannot use `self` or the instance variables of the
frame's receiver (it is compiled outside the method); pass through a variable
of the frame instead.

## Sources without a file on your side

A stack frame, a breakpoint or a Loaded Scripts entry names its source by
`path` when you can open that file yourself. Otherwise the adapter hands out a
`sourceReference` and serves the text through the `source` request, and VS Code
opens it read-only. This happens for:

- **The protoST library** when protost runs elsewhere (over ssh, in WSL, in a
  container): the kernel and stdlib `.st` files live under the remote
  `share/protoST/lib`, which no path mapping covers. You can still step into
  `Object>>displayNl` or `Integer>>to:`, read the code and set breakpoints in
  it.
- Any other file under no mapping, and code without a file (a `<...>` source).
  Expressions you evaluate never show up here: stops are suppressed while they
  run.

Kernel and stdlib frames are deemphasized (greyed out in the call stack) both
ways. **Loaded Scripts** (the Run and Debug view's "Loaded Scripts" section,
from the `loadedSources` request and `loadedSource` events) lists the program,
every kernel file and every module the program imports, as it loads.

Breakpoints work in every loaded file, not only the program: in an imported
module (set it before the import runs: it shows unverified until the module
loads, then turns verified) and in a kernel or stdlib file, by path or by its
sourceReference.

## Remote debugging

The debuggee runs on another machine (a Linux server, WSL, a container) while
you edit and debug in VS Code on yours.

**Option 1 — VS Code's remote extensions (Remote - SSH, WSL, Dev
Containers).** Install protoST and the protoST extension (the `.vsix`) on the
remote side, open the remote folder, press F5. The extension declares
`"extensionKind": ["workspace"]`, so VS Code runs it next to the files and
protost: nothing else to configure.

**Option 2 — `remote.command` + `pathMappings`.** Keep VS Code and the
extension local and let the extension start `protost --dap` through a command
prefix. The adapter translates every path itself:

```json
{
  "type": "protost",
  "request": "launch",
  "name": "protoST over SSH",
  "program": "${file}",
  "cwd": "${fileDirname}",
  "remote": {
    "command": ["ssh", "-T", "-o", "BatchMode=yes", "user@host"],
    "path": "/home/user/.local/bin/protost"
  },
  "pathMappings": [
    { "localRoot": "${workspaceFolder}", "remoteRoot": "/home/user/project" }
  ]
}
```

In WSL the prefix is `["wsl", "-d", "Ubuntu-24.04", "--"]` and `remoteRoot` the
Linux checkout (`/home/user/project`, or `/mnt/c/Users/me/project` to use the
Windows files themselves); in a container, `["docker", "exec", "-i",
"<container>"]`. The extension runs `command[0]` with the rest of `command`,
then `remote.path` (default `protost`) and `--dap`. A `remote` launch without
`pathMappings` is refused with an explanation.

- `pathMappings` is a list of `{localRoot, remoteRoot}`: `localRoot` is your
  side, `remoteRoot` the side protost runs on. The longest matching root wins;
  `\` and `/` are accepted either way, and a Windows root (`c:\...`) matches
  in any letter case. `program`, `cwd` and breakpoint paths go from local to
  remote, every path in the stack, breakpoints and Loaded Scripts from remote
  to local; a file under no mapping is served by `sourceReference` (above).
  An entry without `localRoot` or `remoteRoot` fails the launch.
- **The remote shell must be quiet.** The protocol runs over the command's
  stdin/stdout: anything a login script prints on a non-interactive start
  (a motd, an `echo` in `.bashrc`) corrupts it and the session drops at once.
  Use `ssh -T -o BatchMode=yes` (no terminal, no password prompt: set up key
  authentication), and guard any output in the shell's startup files with
  `[[ $- == *i* ]]`. If protost needs `LD_LIBRARY_PATH` there, put it in the
  prefix: `["ssh", "-T", "user@host", "env", "LD_LIBRARY_PATH=/opt/proto/lib"]`.

`pathMappings` is the adapter's, so any DAP client can use it, not only this
extension.

## JetBrains IDEs (IntelliJ IDEA, PyCharm, CLion, …)

JetBrains IDEs have no protoST plugin, but the
[LSP4IJ](https://github.com/redhat-developer/lsp4ij) plugin (Red Hat, from the
JetBrains Marketplace) adds a **Debug Adapter Protocol** run/debug
configuration that can drive `protost --dap`:

1. Install LSP4IJ (Settings → Plugins → Marketplace).
2. Run → Edit Configurations… → + → **Debug Adapter Protocol**.
3. **Server** tab: the command `protost --dap` (the full path to `protost` if
   it is not on the IDE's PATH), or the remote form, e.g.
   `ssh -T -o BatchMode=yes user@host /home/user/.local/bin/protost --dap`.
4. **Mappings** tab: map the `*.st` file pattern (or the `smalltalk`
   language) to this server, so you can set breakpoints in `.st` files.
5. **Configuration** tab: the working directory, the file to debug, launch
   mode, and the launch parameters as JSON — the same attributes as a VS Code
   `launch.json`:

   ```json
   {
     "request": "launch",
     "program": "${file}",
     "cwd": "${workspaceFolder}",
     "stopOnEntry": false,
     "pathMappings": [{ "localRoot": "/path/on/this/machine", "remoteRoot": "/path/where/protost/runs" }]
   }
   ```

Everything the adapter does — conditions, logpoints, exception filters,
threads, `sourceReference` sources — comes from the adapter, so it is the same
as in VS Code. LSP4IJ variable names (`${file}` and the like) may differ from
VS Code's; use plain paths if they are not substituted.

**Syntax highlighting**: IntelliJ does not know `.st` files. Import the
TextMate grammar shipped with the VS Code extension: Settings → Editor →
TextMate Bundles → + → choose the `editor-integration/vscode` folder (or the
installed `share/protoST/editor-integration/vscode`).

This section was written from LSP4IJ's documentation and the adapter's
protocol behaviour; it has **not** been clicked through in a JetBrains IDE, and
the menu and field names may differ between plugin versions.

## Known limitations

- **Values are flat.** Objects are shown by their print string (`a Counter`)
  and cannot be expanded into their instance variables.
- **`evaluate` binds the frame's variables by value.** An assignment in the
  Debug Console does not change the frame's variable (sending a message that
  mutates the object it names does).
- **Threads blocked in native code are not shown as such.** A worker waiting
  for work, or a thread blocked in a Future's `wait` or in I/O, is listed as
  running; Pause stops it only once it runs script code again.
- **A stop on the main thread while it drains actors** shows the actor's frames
  above the frames of the `wait` that ran them, all as thread `main`.
- **Raised Exceptions also stops in kernel code** that signals and handles an
  exception itself (for instance `sort` falling back to comparing print
  strings when two elements cannot be compared).
- **Conditions cannot use `self`** or the receiver's instance variables.
- **One program per session**: no `attach`, no restart, no function
  breakpoints, no data breakpoints.
- **Remote sessions need key-based ssh** (or another prompt-free command) and
  a quiet remote shell; the extension cannot answer a password prompt.

The DAP exchange is verified by the integration tests `cli_dap_session`,
`cli_dap_output`, `cli_dap_gc`, `cli_dap_exceptions`, `cli_dap_stepping`,
`cli_dap_threads`, `cli_dap_conditions` and `cli_dap_sources`.

## Verifying the adapter without VS Code

You can drive a full session from a script. The integration test
`tests/cli/test_cli_dap_session.sh` does exactly this: `initialize` → `launch`
→ `setBreakpoints` → `configurationDone` → `stopped` → `stackTrace` → `scopes`
→ `variables` → `evaluate` → `continue` → `terminated`. Run it with:

```bash
bash tests/cli/test_cli_dap_session.sh ./build/protost
```

`tests/cli/dap_client.py` is a small reusable client (a reader thread, waits
with deadlines) that the exception, stepping and thread tests are written
with; `test_cli_dap_threads.sh` shows a two-thread session step by step.
