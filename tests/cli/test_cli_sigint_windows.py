#!/usr/bin/env python3
"""cli_sigint on Windows: Ctrl-C interrupts a running evaluation.

The same two cases as test_cli_sigint.sh, with Windows' Ctrl-C (a console
control event) in place of SIGINT:

  * a script interrupted by Ctrl-C ends as the platform's default Ctrl-C ends
    a program: exit status STATUS_CONTROL_C_EXIT (0xC000013A) -- the
    counterpart of POSIX's death by SIGINT (status 130 in a shell);
  * in the REPL, Ctrl-C interrupts an evaluation and the session goes on,
    also when user code wraps the loop in an Error handler. The REPL is
    started with Ctrl-C ignored, as ctest starts every test (a new process
    group) and as a program started in the background inherits it: an
    interactive REPL takes Ctrl-C all the same, as it does on POSIX, where it
    installs its own SIGINT handler over an inherited "ignore".

Git Bash cannot deliver Ctrl-C to a native program (its kill -INT, pgrep and
mkfifo are for MSYS processes), so this runs in a console of its own and
raises the event with GenerateConsoleCtrlEvent, which reaches every process
of that console.

Usage: test_cli_sigint_windows.py <protost>
"""
import os
import subprocess
import sys
import tempfile
import time

PROTOST = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
STATUS_CONTROL_C_EXIT = 0xC000013A
CTRL_C_EVENT = 0


def outer():
    # A console of our own: the Ctrl-C events reach nobody else.
    log = tempfile.mktemp(prefix="protost-sigint-", suffix=".log")
    p = subprocess.Popen([sys.executable, os.path.abspath(__file__), PROTOST, "--inner", log],
                         creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        rc = p.wait(timeout=120)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = 1
    try:
        with open(log, "r", encoding="utf-8") as f:
            sys.stdout.write(f.read())
    except OSError:
        sys.stdout.write("FAIL: the console session wrote no report\n")
        rc = rc or 1
    sys.exit(rc)


def inner(log):
    import ctypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    report = open(log, "w", encoding="utf-8", buffering=1)

    def fail(msg):
        report.write("FAIL: %s\n" % msg)
        sys.exit(1)

    def ctrl_c():
        # This process ignores it (the flag set below); protost does not.
        if not k32.GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0):
            fail("GenerateConsoleCtrlEvent: error %d" % ctypes.get_last_error())

    work = tempfile.mkdtemp(prefix="protost-sigint-")

    # --- a script: started as from a console, with Ctrl-C processed --------
    k32.SetConsoleCtrlHandler(None, False)
    script = subprocess.Popen([PROTOST, "-e", "[true] whileTrue: []"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    k32.SetConsoleCtrlHandler(None, True)       # from here on, ignored here
    time.sleep(1.5)
    ctrl_c()
    try:
        rc = script.wait(timeout=20)
    except subprocess.TimeoutExpired:
        script.kill()
        fail("the script did not end on Ctrl-C")
    if (rc & 0xFFFFFFFF) != STATUS_CONTROL_C_EXIT:
        fail("script exit status 0x%08X, not STATUS_CONTROL_C_EXIT" % (rc & 0xFFFFFFFF))

    # --- the REPL, started with Ctrl-C ignored (inherited) ------------------
    out_path = os.path.join(work, "out")
    with open(out_path, "wb") as out:
        repl = subprocess.Popen([PROTOST, "-i"], stdin=subprocess.PIPE, stdout=out,
                                stderr=subprocess.STDOUT,
                                env=dict(os.environ, HOME=work, USERPROFILE=work))
        repl.stdin.write(b"[[true] whileTrue: []] on: Error do: [:e | 0].\n")
        repl.stdin.flush()
        time.sleep(1.5)
        ctrl_c()
        time.sleep(1.5)
        repl.stdin.write(b"3 + 4.\n:quit\n")
        repl.stdin.flush()
        repl.stdin.close()
        try:
            rc = repl.wait(timeout=20)
        except subprocess.TimeoutExpired:
            repl.kill()
            rc = None
    with open(out_path, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    if rc != 0 or "Interrupted" not in text or "7" not in text:
        fail("repl rc=%r: %r" % (rc, text))
    report.write("OK\n")
    sys.exit(0)


if __name__ == "__main__":
    if not PROTOST:
        sys.exit(__doc__)
    if os.name != "nt":
        sys.exit("this is the Windows variant; run test_cli_sigint.sh elsewhere")
    if len(sys.argv) > 3 and sys.argv[2] == "--inner":
        try:
            inner(sys.argv[3])
        except SystemExit:
            raise
        except BaseException as e:  # report, never hang the outer process
            with open(sys.argv[3], "a", encoding="utf-8") as f:
                f.write("FAIL: %r\n" % (e,))
            sys.exit(1)
    else:
        outer()
