# Chapter 12 — Tooling

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 11](11-advanced-object-model.md) · Next: [Chapter 13 — A worked example](13-worked-example.md)

---

You have the language. This chapter covers the tools that surround it: the
interactive REPL and its meta-commands, the source-level debugger, and the
virtual-environment system for isolating projects. None of this is the language
proper — it is the workbench you build protoST programs on.

## 12.1 The three ways to run code, revisited

[Chapter 1](01-introduction.md) introduced the `protost` CLI. The full set of
invocations:

| Invocation | Effect |
|------------|--------|
| `protost script.st` | run a script; print its last top-level statement |
| `protost -e '<expr>'` | evaluate one expression; print the result |
| `protost -i` | start the interactive REPL |
| `protost -d script.st` | run a script under the CLI debugger |
| `protost --dap` | run the Debug Adapter Protocol server (for VS Code) |
| `protost --dump-ast script.st` | parse and print the AST (a development aid) |
| `protost venv <subcommand>` | manage virtual environments |
| `protost --help` / `--version` | usage / version |

The first three you already use. This chapter is about the REPL in depth, the
debugger, and `venv`.

## 12.2 The REPL

`protost -i` starts a read-eval-print loop:

```
$ ./build/protost -i
protoST 0.1.0-pre — interactive REPL
:help for commands, :quit or Ctrl-D to exit
protoST> 3 + 4
=> 7
protoST> x := 10
=> 10
protoST> x * x
=> 100
```

Two properties make it pleasant to use.

**It is persistent.** A variable, class, or method you define at one prompt is
still there at the next. The session accumulates state — you build up a program
incrementally, trying each piece.

**It detects incomplete input.** If you type something unfinished — an open
`[`, an open `(`, the start of a multi-line method — the REPL does not error.
It shows a continuation prompt (`   ...>`) and keeps reading until the input is
complete. So you can paste or type a whole multi-line method definition at the
prompt and it does the right thing.

> **In Python** this is the `python` shell or a Jupyter cell; **in JavaScript**,
> the `node` REPL or the browser console. The protoST REPL works the same way
> and is the fastest way to try the snippets in this tutorial — most of the
> one-liners shown with `-e` can equally be typed at a `protoST>` prompt.

### Meta-commands

The REPL recognises **meta-commands** — lines beginning with `:` — which are
not protoST code but instructions to the REPL itself. They work only at the
primary prompt, never mid multi-line input.

| Command | Effect |
|---------|--------|
| `:help`, `:h` | list the meta-commands |
| `:quit`, `:q` | exit the REPL (`Ctrl-D` also exits) |
| `:load <path>` | read a `.st` file and execute it *in the current session* — its definitions and variables persist exactly as if you had typed them |
| `:reset` | discard all session state — every user variable, class, and method — and start a fresh runtime |
| `:vars`, `:env` | list the user-defined globals in the session, each with a short value rendering (built-in names are not shown) |
| `:time <expr>` | evaluate `<expr>`, print its result, then report the wall-clock time it took |
| `:history` | show the most recent input lines |

`:load` is the one you will use most: it lets you keep your code in a file,
edit it in your editor, and reload it into a live session to try things — the
edit/reload/experiment loop. `:time` is a quick benchmark:

```
protoST> :time 100 factorial
=> 93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000
time: 0.895 ms
```

`:reset` gives you a clean slate without quitting — useful when an experiment
has left the session in a confusing state.

> **In Python** the analogues are IPython's `%magic` commands — `%load`,
> `%timeit`, `%reset`. protoST's meta-commands are deliberately the same idea:
> a small set of `:`-prefixed REPL conveniences, distinct from the language.
> They are a REPL-only feature — they have no effect on `protost script.st` or
> `protost -e`.

## 12.3 The debugger

protoST scripts can be debugged at source level. There are two front-ends to
the same underlying debugger.

**The CLI debugger** — `protost -d script.st` — runs a script under an
interactive command-line debugger.

**The DAP adapter** — `protost --dap` — speaks the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/),
the JSON-over-stdio protocol that VS Code and other editors use to talk to
debuggers. You do not run `--dap` by hand; VS Code spawns it for you, through a
small extension that ships in `editor-integration/vscode/`. With the extension
installed and a `launch.json` configured, you set breakpoints in the editor
gutter of a `.st` file and press F5.

What the debugger gives you:

- **Breakpoints by line** — set and clear them in the gutter; the adapter maps
  them to source lines via the bytecode's pc-to-line table.
- **Stepping** — step in, step over, step out, continue.
- **A call stack** — named frames with source file and line.
- **A variables panel** — locals and arguments with their real names and
  current values.
- **Watch / evaluate** — type an expression in the Debug Console or Watch
  panel; it is evaluated in the selected frame's scope.

The debugger is a minimum-viable adapter, and `docs/debugging.md` is candid
about its current limits: it surfaces a single logical thread (concurrent
multi-actor debugging is beyond the MVP), `next` currently behaves like
`stepIn`, and most stops show a single stack frame in practice. For the full
setup walkthrough — building `protost`, installing the VS Code extension,
writing `launch.json` — see [`docs/debugging.md`](../debugging.md). A
ready-to-use configuration is in `examples/.vscode/launch.json`.

> **In Python** this is `pdb` (command-line) or the VS Code Python debugger
> (DAP). **In JavaScript**, the `node --inspect` / Chrome DevTools combination.
> protoST's story is the familiar one: a command-line debugger for quick work,
> and a DAP adapter so your editor's debugging UI works on `.st` files just as
> it does on `.py` or `.js`.

## 12.4 Virtual environments

protoST ships a Python-style **virtual environment** (venv) mechanism for
isolating projects: a `.venv/` directory with its own installed modules,
configuration, and bytecode cache, so two projects on one machine do not
interfere.

You create one with `venv create`:

```bash
$ ./build/protost venv create .venv
```

This builds a `.venv/` directory with `bin/`, `lib/`, and `cache/`
subdirectories and a `stenv.cfg` configuration file. The other subcommands:

| Command | Effect |
|---------|--------|
| `protost venv create [path]` | create a venv (default `.venv`) |
| `protost venv activate [path]` | print the shell snippet to `source` |
| `protost venv info` | show the active venv (or "no active venv") |

The runtime discovers an active venv via the `STENV` environment variable, then
by walking up from the working directory looking for a `.venv/`, then a home
venv, then system defaults. When a venv is active, `Import from:` resolves
modules from the venv's `lib/` — so a project can pin its own module versions.

> **In Python** this is `python -m venv .venv` then `source
> .venv/bin/activate` — and protoST's `venv` deliberately mirrors it, command
> for command. **In JavaScript** the analogue is a project's local
> `node_modules/`. The purpose is identical everywhere: project-local
> isolation so dependencies do not collide.
>
> The venv layout and the `create` / `activate` / `info` subcommands work
> today. The `install` / `freeze` subcommands and a project manifest file are
> planned but not yet present — see [`docs/LANGUAGE.md`](../LANGUAGE.md) §11.3.

## 12.5 One process, one runtime

A practical constraint worth knowing, though it rarely affects you directly: a
protoST runtime must be the **only** one in its operating-system process. The
`protost` CLI always constructs exactly one, so running scripts, the REPL, and
the debugger is unaffected. The constraint matters only if you *embed* protoST
as a library — and even then it is straightforward: construct one runtime per
process. `docs/STATUS.md` records this as intentional deviation D2; it stems
from protoCore's per-process symbol-interning caches.

## 12.6 Summary

- The `protost` CLI runs scripts (`script.st`), one-liners (`-e`), the REPL
  (`-i`), and the debugger (`-d` / `--dap`), and manages venvs (`venv`).
- The **REPL** is persistent and detects incomplete input. Its **meta-commands**
  — `:load`, `:reset`, `:vars`, `:time`, `:history`, `:help`, `:quit` — are
  `:`-prefixed REPL conveniences, distinct from the language. `:load` powers
  the edit/reload/experiment loop.
- The **debugger** offers breakpoints, stepping, a call stack, a variables
  panel, and watch/evaluate — as a CLI tool (`-d`) and as a VS Code DAP adapter
  (`--dap`). See [`docs/debugging.md`](../debugging.md).
- **Virtual environments** (`venv create` / `activate` / `info`) isolate a
  project's modules, mirroring Python's `venv`.

---

Next: [Chapter 13 — A worked example](13-worked-example.md)
