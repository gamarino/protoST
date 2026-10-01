#!/usr/bin/env python3
"""Run the code examples in protoST's Markdown documentation and check them.

Usage
-----
    run_doc_snippets.py [options] <protost-binary> <file.md> [<file.md> ...]

Options
    --root DIR      repository root (default: two levels above this script).
                    Doc commands that name an existing repository file, such
                    as ``examples/pump_twin.st``, resolve against it.
    --lib DIR       value for PROTOST_LIB (default: <root>/lib).
    --timeout SEC   per-run timeout in seconds (default: 20).
    --work-dir DIR  where snippet files are written (default: a fresh
                    temporary directory, deleted afterwards).
    -v, --verbose   also list every PASS and every unchecked block.

Every run goes through ``timeout <SEC>`` and runs are strictly sequential.
The exit status is 1 when any example fails, 2 on a usage error, 0 otherwise.

What counts as an example
-------------------------
An *example* is a piece of runnable code together with a result the document
states for it. The tool recognises the conventions the protoST documents use:

1. Inline value annotations inside a ``smalltalk`` block. A trailing comment
   on the line where a top-level statement ends states that statement's value:

       3 + 4        "=> 7"
       3 factorial printString    "= (3 factorial) printString => '6'"
       2 + 3 * 4    "= (2 + 3) * 4 = 20"
       d describe.  "evaluates to 'an animal that barks'"
       Counter new total printNl.   "-> 3"      (written with the arrow U+2192)
       c incr(1) printNl.            "7 - factor defaults to 1"

   Accepted comment forms: ``=> VALUE`` anywhere in the comment (the last
   ``=>`` wins); ``evaluates to VALUE`` at the start of the comment; a
   precedence walk-through ``= step = ... = VALUE`` (the last step is the
   value, and it needs at least one intermediate step); the arrow U+2192
   followed by VALUE at the start of the comment; and, on a statement that
   ends in ``printNl``, a comment that starts with VALUE. For the last three
   forms VALUE must look like a literal (a number, a quoted string, a
   symbol, true/false/nil, or "a Foo"/"an Foo"), so prose comments such as
   ``"High finishes first"`` are not mistaken for results.

   VALUE is cut at the first " -- " style dash (em dash, en dash or "--")
   and at the first top-level ", " or "; ", so ``"=> 14, not 11"`` states 14.
   VALUE is written in Smalltalk literal notation: a quoted string ``'ab'``
   or a symbol ``#foo`` is compared without its quotes / hash, because the
   runtime displays a String or Symbol value without them.

   Each annotated statement S is checked with its own run: the block (or
   snippet group, see below) is cut right after S, so S becomes the last
   top-level statement, and it is run with ``protost --print-last``, which
   prints that value after the program -- no probe code that could depend on
   the printing protocol under test. The value is the
   trailing lines of standard output (as many lines as VALUE has); output
   printed earlier by the program is ignored.

   Statement boundaries: S starts after the nearest preceding top-level
   period, blank line, or comment that ends a line of code. Documents often
   list independent one-line expressions, each with a trailing comment and no
   period; so that the cut program parses, a period is inserted after such a
   line's code when the next non-blank line starts in column 0 (an indented
   next line is taken as a continuation or a method body). An annotated
   statement whose run fails before printing a value fails with the run's
   error output as "got".

2. Shell transcripts in ``bash``/``sh``/``console``/untagged blocks: a line
   ``$ [time] [VAR=value ...] ./build/protost ARGS`` followed by the lines it
   prints (up to the next ``$`` line). The command is split with shell rules
   (``shlex``), exactly as a reader pasting it would get it, and run with the
   binary under test. ``real``/``user``/``sys`` lines of ``time`` are dropped.
   Standard output and standard error are merged (the reader sees both);
   standard output is line-buffered via ``stdbuf -oL`` to keep the order.

   A script argument ``name.st`` resolves, in order, to (a) the nearest
   ``smalltalk`` block in the same document whose first line is a header
   comment ``"-- name.st ..."`` (that block is written to ``name.st`` in a
   scratch directory and run from there), or (b) an existing file relative
   to the repository root. A command with no output lines is not checked
   and is counted as unchecked.

3. REPL transcripts: ``$ ./build/protost -i`` followed by the session, or a
   block whose first line starts with ``protoST> ``. The inputs after each
   ``protoST> `` prompt are fed on standard input, and the output is rebuilt
   as a transcript (prompt + input + response) for comparison. A ``time: N
   ms`` line (the ``:time`` meta-command) is dropped on both sides because it
   is not deterministic. A transcript without the ``$ ... -i`` line is
   compared without the startup banner.

4. A ``smalltalk`` block immediately followed by a paragraph that starts with
   ``prints `X` `` is run as a script and must print exactly X.

Comparison normalises trailing whitespace only: trailing spaces on each line
and trailing blank lines are ignored.

Blocks that are not examples
----------------------------
* A fence tagged ``smalltalk no-run`` (the tag after the language name) is
  skipped and counted as "skipped".
* A ``smalltalk`` block with no stated result that is not otherwise used (as
  a named script or as part of a snippet group with results) is counted as
  "unchecked" and reported, so drift cannot hide in silence. So is a
  ``$ protost`` command that shows no output.

Snippet groups
--------------
Some examples build up across several blocks: a class defined in one block
and used in the next. Put this HTML comment (invisible when rendered) on the
line directly above each fence of such a sequence, with no blank line in
between:

    <!-- snippet-group: NAME -->

All ``smalltalk`` blocks of one document that share NAME are concatenated in
document order (separated by a blank line, which also ends any open method
body) into one program; each annotation in any member is checked against
that program cut after the annotated statement. A header-named script (convention 2) that belongs to a group
is written as the concatenation of the group members up to and including it.
Group names are local to one document. As of 2026-09 the markers are used in
docs/tutorial/05-classes-and-methods.md (class-variables, call-form),
docs/tutorial/10-actors-and-futures.md (atom) and docs/LANGUAGE.md (atom).
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------
# Markdown parsing
# --------------------------------------------------------------------------

FENCE_RE = re.compile(r"^(\s*)(`{3,}|~{3,})\s*([^`]*)$")
GROUP_RE = re.compile(r"^\s*<!--\s*snippet-group:\s*([\w.-]+)\s*-->\s*$")
HEADER_RE = re.compile(r'^\s*"--\s*([\w./-]+\.st)\b')
PRINTS_PROSE_RE = re.compile(r"^\s*(?:this\s+)?prints\s+`([^`]*)`", re.IGNORECASE)
SHELL_LANGS = {"", "bash", "sh", "shell", "console", "text"}


@dataclass
class Block:
    lang: str
    tags: list[str]
    fence_line: int            # 1-based line of the opening fence
    lines: list[str]           # body lines, indentation of the fence removed
    group: str | None = None
    next_para: str = ""        # first non-blank line after the closing fence
    used: bool = False         # consumed by some example

    @property
    def first_line(self) -> int:
        return self.fence_line + 1

    @property
    def text(self) -> str:
        return "\n".join(self.lines) + "\n"

    @property
    def no_run(self) -> bool:
        return "no-run" in self.tags


def parse_markdown(path: Path) -> list[Block]:
    raw = path.read_text(encoding="utf-8").split("\n")
    blocks: list[Block] = []
    i = 0
    while i < len(raw):
        m = FENCE_RE.match(raw[i])
        if not m:
            i += 1
            continue
        indent, fence, info = m.group(1), m.group(2), m.group(3).strip()
        words = info.split()
        lang = words[0].lower() if words else ""
        tags = [w.lower() for w in words[1:]]
        j = i + 1
        body = []
        while j < len(raw):
            if raw[j].strip().startswith(fence[0] * len(fence)) and raw[j].strip().strip(fence[0]) == "":
                break
            line = raw[j]
            body.append(line[len(indent):] if line.startswith(indent) else line.lstrip())
            j += 1
        group = None
        if i > 0:
            gm = GROUP_RE.match(raw[i - 1])
            if gm:
                group = gm.group(1)
        nxt = ""
        for k in range(j + 1, min(j + 4, len(raw))):
            if raw[k].strip():
                nxt = raw[k]
                break
        blocks.append(Block(lang, tags, i + 1, body, group, nxt))
        i = j + 1
    return blocks


# --------------------------------------------------------------------------
# Smalltalk lexical scan (just enough to find comments and statement ends)
# --------------------------------------------------------------------------

def scan_smalltalk(text: str) -> tuple[list[tuple[int, int]], list[int]]:
    """Return (comment spans [start, end), top-level period offsets)."""
    comments: list[tuple[int, int]] = []
    periods: list[int] = []
    depth = 0
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = text.find('"', i + 1)
            j = n - 1 if j < 0 else j
            comments.append((i, j + 1))
            i = j + 1
            continue
        if c == "'":
            j = i + 1
            while j < n:
                if text[j] == "'":
                    if j + 1 < n and text[j + 1] == "'":
                        j += 2
                        continue
                    break
                j += 1
            i = j + 1
            continue
        if c == "$":            # character literal: the next char is literal
            i += 2
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth = max(0, depth - 1)
        elif c == "." and depth == 0:
            is_decimal = 0 < i < n - 1 and text[i - 1].isdigit() and text[i + 1].isdigit()
            if not is_decimal:
                periods.append(i)
        i += 1
    return comments, periods


LITERALISH_RE = re.compile(
    r"^(-?\d[\d.]*|'(?:[^']|'')*'|#\S+|true|false|nil|an? [A-Z]\w*.*)$")


def _cut_value(v: str) -> str:
    """Trim prose that follows a stated value inside an annotation comment."""
    for sep in (" — ", " – ", " -- "):
        k = v.find(sep)
        if k >= 0:
            v = v[:k]
    out, depth, quoted = [], 0, False
    for idx, ch in enumerate(v):
        if ch == "'":
            quoted = not quoted
        elif not quoted:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch in ",;" and depth == 0 and (idx + 1 == len(v) or v[idx + 1] == " "):
                break
        out.append(ch)
    return "".join(out).strip()


def _literal_to_display(v: str) -> str:
    """Map a value written in literal notation to its displayed form."""
    m = re.fullmatch(r"'((?:[^']|'')*)'", v)
    if m:
        return m.group(1).replace("''", "'")
    m = re.fullmatch(r"#([A-Za-z_][\w:]*|[-+*/\\<>=~@%|&?,]+)", v)
    if m:
        return m.group(1)
    return v


def annotation_value(comment_body: str, statement: str) -> str | None:
    """Return the stated value of an annotation comment, or None."""
    body = comment_body.strip()
    if "=>" in body:
        v = _cut_value(body[body.rindex("=>") + 2:].strip())
        return _literal_to_display(v) if v else None
    m = re.match(r"^evaluates to\s+(.+)$", body)
    if m:
        v = _cut_value(m.group(1))
        return _literal_to_display(v) if v else None
    if body.startswith("= "):
        # Precedence walk-through: "= (2 + 3) * 4 = 20" -- the last step is the value.
        parts = body[2:].split(" = ")
        v = _cut_value(parts[-1]) if len(parts) >= 2 else ""
        return _literal_to_display(v) if v and LITERALISH_RE.match(v) else None
    if body.startswith("→"):
        v = _cut_value(body[1:].strip())
        return _literal_to_display(v) if LITERALISH_RE.match(v) else None
    if re.search(r"\bprintNl\s*\.?\s*$", statement):
        v = _cut_value(body)
        return _literal_to_display(v) if v and LITERALISH_RE.match(v) else None
    return None


@dataclass
class Annotation:
    index: int
    start: int          # offset of the statement start in the program
    end: int            # offset just after the statement (period excluded)
    comment_start: int
    expected: str
    line: int           # 1-based line in the program


def find_annotations(text: str) -> tuple[list[Annotation], list[int]]:
    """Return the annotated statements of a program and the offsets where a
    period must be inserted to terminate an unterminated one-line expression."""
    comments, periods = scan_smalltalk(text)
    comment_at = {s: e for (s, e) in comments}
    blank_ends = [m.end() for m in re.finditer(r"\n[ \t]*\n", text)]
    result: list[Annotation] = []
    inserts: list[int] = []
    boundary = 0      # end of the latest comment that ends a line of code
    for (cs, ce) in comments:
        line_start = text.rfind("\n", 0, cs) + 1
        code = text[line_start:cs]
        if not code.strip():
            continue
        line_end = text.find("\n", ce)
        line_end = len(text) if line_end < 0 else line_end
        ends_line = not text[ce:line_end].strip()
        end = line_start + len(code.rstrip())
        terminated = end - 1 in periods
        stmt_end = end - 1 if terminated else end
        while stmt_end > line_start and text[stmt_end - 1].isspace():
            stmt_end -= 1
        start = max([0, boundary]
                    + [p + 1 for p in periods if p < stmt_end]
                    + [b for b in blank_ends if b <= stmt_end])
        while start < stmt_end:          # skip whitespace and leading comments
            if text[start].isspace():
                start += 1
            elif start in comment_at and comment_at[start] <= stmt_end:
                start = comment_at[start]
            else:
                break
        statement = text[start:stmt_end]
        expected = annotation_value(text[cs + 1:ce - 1], statement)
        if ends_line:
            boundary = ce
            nxt = re.search(r"\S", text[line_end:])
            next_in_col0 = nxt is None or text[line_end + nxt.start() - 1] == "\n"
            if not terminated and next_in_col0:
                inserts.append(stmt_end)
        if expected is None:
            continue
        result.append(Annotation(len(result) + 1, start, stmt_end, cs, expected,
                                 text.count("\n", 0, cs) + 1))
    return result, inserts


def cut_program(text: str, inserts: list[int], anno: Annotation) -> str:
    """The program up to and including the annotated statement, which becomes
    the last top-level statement. Line numbers are preserved."""
    pieces, prev = [], 0
    for p in sorted(x for x in inserts if x < anno.end):
        pieces += [text[prev:p], "."]
        prev = p
    pieces += [text[prev:anno.end], ".\n"]
    return "".join(pieces)


# --------------------------------------------------------------------------
# Running
# --------------------------------------------------------------------------

@dataclass
class RunResult:
    stdout: str          # standard output (merged with standard error if asked)
    returncode: int
    timed_out: bool
    stderr: str = ""

    def status_note(self) -> str:
        if self.timed_out:
            return "TIMEOUT (possible hang)"
        if self.returncode < 0 or self.returncode >= 128:
            sig = -self.returncode if self.returncode < 0 else self.returncode - 128
            return f"CRASH (signal {sig})"
        return ""


class Runner:
    def __init__(self, binary: Path, root: Path, lib: Path, timeout: int, work: Path):
        self.binary = binary
        self.root = root
        self.lib = lib
        self.timeout = timeout
        self.work = work
        self.counter = 0
        # POSIX tools. On Windows the ones found are Git's MSYS builds, which
        # cannot line-buffer a native program and get in the way of the
        # programs it starts; subprocess.run's own timeout still applies.
        native_windows = os.name == "nt"
        self.stdbuf = None if native_windows else shutil.which("stdbuf")
        self.timeout_cmd = None if native_windows else shutil.which("timeout")

    def scratch_dir(self) -> Path:
        self.counter += 1
        d = self.work / f"run{self.counter:04d}"
        d.mkdir(parents=True, exist_ok=True)
        return d

    def run(self, args: list[str], cwd: Path, env_extra: dict[str, str] | None = None,
            stdin: str | None = None, merge_stderr: bool = True) -> RunResult:
        env = dict(os.environ)
        env["PROTOST_LIB"] = str(self.lib)
        env.update(env_extra or {})
        cmd = [str(self.binary)] + args
        if self.stdbuf:
            cmd = [self.stdbuf, "-oL", "-eL"] + cmd
        if self.timeout_cmd:
            cmd = [self.timeout_cmd, str(self.timeout)] + cmd
        try:
            p = subprocess.run(cmd, cwd=cwd, env=env, input=stdin if stdin is not None else "",
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT if merge_stderr else subprocess.PIPE,
                               encoding="utf-8", errors="replace", timeout=self.timeout + 10)
        except subprocess.TimeoutExpired as e:
            out = e.stdout.decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
            return RunResult(out, 124, True)
        return RunResult(p.stdout, p.returncode, p.returncode == 124, p.stderr or "")


def normalise(text: str) -> str:
    lines = [ln.rstrip() for ln in text.replace("\r\n", "\n").split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------

@dataclass
class Outcome:
    file: str
    line: int
    kind: str
    source: str
    expected: str
    got: str
    ok: bool
    note: str = ""


@dataclass
class FileReport:
    path: str
    outcomes: list[Outcome] = field(default_factory=list)
    unchecked: list[tuple[int, str]] = field(default_factory=list)
    skipped: int = 0


def short(text: str, width: int = 70) -> str:
    one = " ".join(text.split())
    return one if len(one) <= width else one[:width - 3] + "..."


class DocChecker:
    def __init__(self, runner: Runner, root: Path):
        self.runner = runner
        self.root = root

    # -- helpers -----------------------------------------------------------

    @staticmethod
    def program_for(blocks: list[Block]) -> tuple[str, list[int]]:
        """Concatenate blocks; return the text and a map program-line -> doc-line."""
        parts, line_map = [], []
        for b in blocks:
            if parts:
                parts.append("\n")
                line_map.append(b.fence_line)   # separator blank line
            parts.append(b.text)
            line_map.extend(b.first_line + k for k in range(len(b.lines)))
        return "".join(parts), line_map

    def group_prefix(self, blocks: list[Block], target: Block) -> list[Block]:
        if target.group is None:
            return [target]
        members = [b for b in blocks if b.group == target.group and b.lang == "smalltalk"
                   and not b.no_run]
        return members[:members.index(target) + 1]

    # -- convention 1: inline annotations ------------------------------------

    def check_annotations(self, rel: str, blocks: list[Block], report: FileReport) -> None:
        units: list[list[Block]] = []
        seen_groups: set[str] = set()
        for b in blocks:
            if b.lang != "smalltalk" or b.no_run:
                continue
            if b.group is None:
                units.append([b])
            elif b.group not in seen_groups:
                seen_groups.add(b.group)
                units.append([x for x in blocks if x.group == b.group
                              and x.lang == "smalltalk" and not x.no_run])
        for unit in units:
            text, line_map = self.program_for(unit)
            annos, inserts = find_annotations(text)
            if not annos:
                continue
            for b in unit:
                b.used = True
            for a in annos:
                d = self.runner.scratch_dir()
                (d / "snippet.st").write_text(cut_program(text, inserts, a), encoding="utf-8")
                res = self.runner.run(["--print-last", "snippet.st"], cwd=d, merge_stderr=False)
                expected = normalise(a.expected)
                note = res.status_note()
                out = normalise(res.stdout)
                if res.returncode == 0 and out:
                    got = "\n".join(out.split("\n")[-len(expected.split("\n")):])
                    ok = got == expected
                else:
                    ok = False
                    got = f"(no value printed; exit status {res.returncode})"
                    if res.stdout.strip():
                        got += "\n[stdout] " + "\n".join(out.split("\n")[-3:])
                    if res.stderr.strip():
                        got += "\n" + "\n".join(normalise(res.stderr).split("\n")[:4])
                report.outcomes.append(Outcome(rel, line_map[a.line - 1], "value",
                                               text[a.start:a.end], expected, got, ok, note))

    # -- convention 4: `prints `X`` prose after a block ----------------------

    def check_prose_prints(self, rel: str, blocks: list[Block], report: FileReport) -> None:
        for b in blocks:
            if b.lang != "smalltalk" or b.no_run:
                continue
            m = PRINTS_PROSE_RE.match(b.next_para)
            if not m:
                continue
            b.used = True
            text, _ = self.program_for(self.group_prefix(blocks, b))
            d = self.runner.scratch_dir()
            (d / "snippet.st").write_text(text, encoding="utf-8")
            res = self.runner.run(["snippet.st"], cwd=d)
            got = normalise(res.stdout)
            report.outcomes.append(Outcome(rel, b.first_line, "prose", b.text, m.group(1),
                                           got, got == normalise(m.group(1)),
                                           res.status_note()))

    # -- conventions 2 and 3: shell and REPL transcripts ---------------------

    def resolve_script(self, name: str, blocks: list[Block], before_line: int) -> Block | None:
        best = None
        for b in blocks:
            if b.lang != "smalltalk" or b.no_run or not b.lines:
                continue
            m = HEADER_RE.match(b.lines[0])
            if m and m.group(1) == name and b.fence_line < before_line:
                best = b
        return best

    def check_shell_block(self, rel: str, blocks: list[Block], blk: Block,
                          report: FileReport) -> None:
        lines = blk.lines
        first = next((ln for ln in lines if ln.strip()), "")
        if first.startswith("protoST> "):
            self.check_repl(rel, blk.first_line, None, lines, report, with_banner=False)
            return
        i = 0
        while i < len(lines):
            if not lines[i].startswith("$ "):
                i += 1
                continue
            cmd_line = lines[i][2:]
            j = i + 1
            while j < len(lines) and not lines[j].startswith("$ "):
                j += 1
            out_lines = [ln for ln in lines[i + 1:j]
                         if not re.match(r"^(real|user|sys)\s+\d", ln)]
            doc_line = blk.first_line + i
            try:
                argv = shlex.split(cmd_line)
            except ValueError:
                argv = []
            env_extra: dict[str, str] = {}
            while argv and (argv[0] == "time" or re.match(r"^[A-Z_][A-Z0-9_]*=", argv[0])):
                tok = argv.pop(0)
                if tok != "time":
                    k, _, v = tok.partition("=")
                    env_extra[k] = v
            if not argv or os.path.basename(argv[0]) != "protost":
                i = j
                continue
            args = argv[1:]
            if args[:1] == ["-i"]:
                self.check_repl(rel, doc_line, cmd_line, lines[i + 1:j], report, with_banner=True)
                i = j
                continue
            expected = normalise("\n".join(out_lines))
            if not expected:
                report.unchecked.append((doc_line, "command without output: " + cmd_line))
                i = j
                continue
            d = self.runner.scratch_dir()
            note = ""
            resolved_args = []
            for a in args:
                if a.endswith(".st") and not a.startswith("-"):
                    src = self.resolve_script(os.path.basename(a), blocks, blk.fence_line)
                    if src is not None and a == os.path.basename(a):
                        src.used = True
                        text, _ = self.program_for(self.group_prefix(blocks, src))
                        (d / a).write_text(text, encoding="utf-8")
                        resolved_args.append(a)
                        continue
                    if (self.root / a).is_file():
                        resolved_args.append(str(self.root / a))
                        continue
                    note = f"script {a!r} not found in the document or the repository"
                resolved_args.append(a)
            if note:
                report.outcomes.append(Outcome(rel, doc_line, "command", cmd_line, expected,
                                               "(not run)", False, note))
                i = j
                continue
            res = self.runner.run(resolved_args, cwd=d, env_extra=env_extra)
            got = normalise(res.stdout)
            report.outcomes.append(Outcome(rel, doc_line, "command", cmd_line, expected, got,
                                           got == expected, res.status_note()))
            i = j

    def check_repl(self, rel: str, doc_line: int, cmd_line: str | None, lines: list[str],
                   report: FileReport, with_banner: bool) -> None:
        def drop_timing(s: str) -> str:
            return "\n".join(ln for ln in s.split("\n") if not re.match(r"^time: [\d.]+ ms$", ln))

        expected = normalise(drop_timing("\n".join(lines)))
        inputs = [ln[len("protoST> "):] for ln in lines if ln.startswith("protoST> ")]
        if not inputs:
            report.unchecked.append((doc_line, "REPL transcript without input"))
            return
        d = self.runner.scratch_dir()
        res = self.runner.run(["-i"], cwd=d, stdin="\n".join(inputs) + "\n")
        segments = res.stdout.split("protoST> ")
        banner, answers = segments[0], segments[1:]
        rebuilt = banner if with_banner else ""
        for k, inp in enumerate(inputs):
            rebuilt += "protoST> " + inp + "\n"
            if k < len(answers):
                rebuilt += answers[k]
        got = normalise(drop_timing(rebuilt))
        report.outcomes.append(Outcome(rel, doc_line, "repl", cmd_line or "(REPL session)",
                                       expected, got, got == expected, res.status_note()))

    # -- driver --------------------------------------------------------------

    def check_file(self, path: Path) -> FileReport:
        try:
            rel = str(path.resolve().relative_to(self.root))
        except ValueError:
            rel = str(path)
        report = FileReport(rel)
        blocks = parse_markdown(path)
        report.skipped = sum(1 for b in blocks if b.lang == "smalltalk" and b.no_run)
        self.check_annotations(rel, blocks, report)
        self.check_prose_prints(rel, blocks, report)
        for b in blocks:
            if b.lang in SHELL_LANGS and not b.no_run:
                self.check_shell_block(rel, blocks, b, report)
        # A group member counts as used when any member of its group is used.
        used_groups = {b.group for b in blocks if b.used and b.group}
        for b in blocks:
            if b.lang == "smalltalk" and not b.no_run and not b.used \
                    and b.group not in used_groups:
                report.unchecked.append((b.first_line, "smalltalk block: " + short(b.text, 50)))
        report.outcomes.sort(key=lambda o: o.line)
        report.unchecked.sort()
        return report


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def indent(text: str, prefix: str = "      | ") -> str:
    return "\n".join(prefix + ln for ln in (text.split("\n") if text else [""]))


def print_report(reports: list[FileReport], verbose: bool) -> int:
    failures = 0
    for r in reports:
        for o in r.outcomes:
            if o.ok:
                if verbose:
                    print(f"PASS {o.file}:{o.line}  [{o.kind}] {short(o.source)}"
                          + (f"  ({o.note})" if o.note else ""))
                continue
            failures += 1
            print(f"FAIL {o.file}:{o.line}  [{o.kind}] {short(o.source)}")
            if o.note:
                print(f"    note: {o.note}")
            print("    expected:")
            print(indent(o.expected))
            print("    got:")
            print(indent(o.got))
        if verbose:
            for line, what in r.unchecked:
                print(f"UNCHECKED {r.path}:{line}  {what}")
    print()
    print(f"{'document':<52} {'examples':>8} {'passed':>7} {'failed':>7} "
          f"{'unchecked':>9} {'skipped':>7}")
    totals = [0, 0, 0, 0, 0]
    for r in reports:
        passed = sum(1 for o in r.outcomes if o.ok)
        row = [len(r.outcomes), passed, len(r.outcomes) - passed, len(r.unchecked), r.skipped]
        totals = [a + b for a, b in zip(totals, row)]
        print(f"{r.path:<52} {row[0]:>8} {row[1]:>7} {row[2]:>7} {row[3]:>9} {row[4]:>7}")
    if len(reports) > 1:
        print(f"{'TOTAL':<52} {totals[0]:>8} {totals[1]:>7} {totals[2]:>7} "
              f"{totals[3]:>9} {totals[4]:>7}")
    return failures


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("binary")
    ap.add_argument("docs", nargs="+")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument("--lib")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--work-dir")
    ap.add_argument("-v", "--verbose", action="store_true")
    ns = ap.parse_args(argv)

    binary = Path(ns.binary).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"error: {binary} is not an executable file", file=sys.stderr)
        return 2
    root = Path(ns.root).resolve()
    lib = Path(ns.lib).resolve() if ns.lib else root / "lib"
    cleanup = ns.work_dir is None
    work = Path(ns.work_dir or tempfile.mkdtemp(prefix="protost-docs-")).resolve()
    work.mkdir(parents=True, exist_ok=True)
    try:
        checker = DocChecker(Runner(binary, root, lib, ns.timeout, work), root)
        reports = []
        for doc in ns.docs:
            p = Path(doc)
            if not p.is_file():
                print(f"error: {doc}: no such file", file=sys.stderr)
                return 2
            reports.append(checker.check_file(p))
        failures = print_report(reports, ns.verbose)
    finally:
        if cleanup:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
