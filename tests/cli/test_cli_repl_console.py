#!/usr/bin/env python3
"""The interactive REPL at a real terminal: a pseudo-terminal on Linux and
macOS, a console of its own on Windows. What a pipe cannot show:

  * non-ASCII input typed at the prompt arrives as UTF-8 ('ñandú' size = 5);
  * Ctrl-C at the prompt cancels the line being typed (and a multi-line form
    being entered) and the session goes on: `1 + ` then Ctrl-C then `3 + 4.`
    answers 7, not 8;
  * Ctrl-C pressed again and again at an empty prompt never ends the session;
  * a second Ctrl-C while an evaluation is blocked where it cannot be
    interrupted (a wait on a Future nobody resolves) ends the process as the
    platform's default Ctrl-C would: killed by SIGINT on POSIX, exit status
    STATUS_CONTROL_C_EXIT (0xC000013A) on Windows.

On Windows the keys are written into the console's input buffer, and Ctrl-C
is pressed on the keyboard (keybd_event to the console window), because only
a real keystroke makes the console abort a pending ReadConsoleW, as a person
pressing Ctrl-C does; GenerateConsoleCtrlEvent only raises the signal.

Usage: test_cli_repl_console.py <protost>
"""
import os
import subprocess
import sys
import tempfile
import time

PROTOST = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
TIMEOUT = 15.0
PRIMARY = "protoST> "
STATUS_CONTROL_C_EXIT = 0xC000013A
# Waiting on a Future nobody resolves: the main thread blocks on a semaphore
# and reaches no safepoint, so a Ctrl-C cannot be taken there.
BLOCKING = "Future new wait.\r"


def fail(msg, transcript, status=None):
    if status is not None:
        msg += " (the process had ended: status %r)" % (status,)
    sys.stdout.write("FAIL: %s\n--- transcript ---\n%s\n" % (msg, transcript))
    sys.exit(1)


class Session:
    """Common driver: `send` types text, `ctrl_c` presses Ctrl-C, `output`
    answers everything the REPL printed so far (decoded as UTF-8)."""

    def expect_after(self, needle, start, timeout=TIMEOUT):
        """Waits for `needle` in the output past offset `start`; answers the
        offset just past it. Offsets, not counts: a prompt is printed after
        a result, a little later, and a count taken in between would be
        satisfied by that prompt instead of the one the action causes."""
        end = time.time() + timeout
        while time.time() < end:
            i = self.output().find(needle, start)
            if i >= 0:
                return i + len(needle)
            if self.exited() is not None:
                break
            time.sleep(0.05)
        fail("waiting for %r" % (needle,), self.output(), self.exited())

    def settled(self):
        """The output's length once nothing more has arrived for a moment."""
        size = -1
        while True:
            time.sleep(0.3)
            now = len(self.output())
            if now == size:
                return now
            size = now

    def evaluate(self, line, result, pos):
        """Types `line`; waits for `result` and the prompt after it."""
        self.send(line)
        pos = self.expect_after(result, pos)
        return self.expect_after(PRIMARY, pos)

    def cancel(self):
        """Presses Ctrl-C at the prompt; waits for the next prompt."""
        mark = self.settled()
        self.ctrl_c()
        return self.expect_after(PRIMARY, mark)

    def scenario(self):
        pos = self.expect_after(PRIMARY, 0)
        pos = self.evaluate("'\u00f1and\u00fa' size.\r", "=> 5", pos)

        # Ctrl-C cancels the line being typed.
        self.send("1 + ")
        pos = self.cancel()
        pos = self.evaluate("3 + 4.\r", "=> 7", pos)
        if "=> 8" in self.output():
            fail("the cancelled line was evaluated", self.output())

        # ... and a multi-line form being entered.
        self.send("[:x |\r")
        pos = self.expect_after("...> ", pos)
        pos = self.cancel()
        pos = self.evaluate("6 * 7.\r", "=> 42", pos)

        # Ctrl-C at an empty prompt, again and again: the session goes on.
        for _ in range(3):
            pos = self.cancel()
        pos = self.evaluate("5 + 5.\r", "=> 10", pos)

        # An evaluation blocked where it reaches no safepoint: the first
        # Ctrl-C stays pending, the second ends the process.
        self.send(BLOCKING)
        time.sleep(1.0)
        self.ctrl_c()
        time.sleep(0.5)
        self.ctrl_c()
        end = time.time() + TIMEOUT
        while self.exited() is None and time.time() < end:
            time.sleep(0.05)
        return self.exited()


# ----------------------------------------------------------------- POSIX


class PtySession(Session):
    def __init__(self):
        import pty
        self.home = tempfile.mkdtemp(prefix="protost-repl-")
        env = dict(os.environ, HOME=self.home, TERM="dumb")
        env["LC_ALL"] = "en_US.UTF-8" if sys.platform == "darwin" else "C.UTF-8"
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(PROTOST, [PROTOST, "-i"], env)
        self.buf = b""
        self.status = None

    def pump(self):
        import select
        while True:
            r, _, _ = select.select([self.fd], [], [], 0)
            if not r:
                return
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                return
            if not data:
                return
            self.buf += data

    def output(self):
        self.pump()
        return self.buf.decode("utf-8", "replace")

    def send(self, text):
        os.write(self.fd, text.encode("utf-8"))

    def ctrl_c(self):
        # The terminal's interrupt character: the line discipline sends
        # SIGINT to the foreground process group, as a keyboard Ctrl-C does.
        os.write(self.fd, b"\x03")

    def exited(self):
        if self.status is None:
            pid, st = os.waitpid(self.pid, os.WNOHANG)
            if pid == self.pid:
                self.status = st
        return self.status


def run_posix():
    import signal
    s = PtySession()
    st = s.scenario()
    if st is None:
        os.kill(s.pid, signal.SIGKILL)
        fail("a second Ctrl-C while blocked did not end the process", s.output())
    if not (os.WIFSIGNALED(st) and os.WTERMSIG(st) == signal.SIGINT):
        fail("ended with wait status %r, not by SIGINT" % st, s.output())
    print("OK")


# --------------------------------------------------------------- Windows


def run_windows_outer():
    # A console of our own, so keystrokes and Ctrl-C reach nobody else.
    log = tempfile.mktemp(prefix="protost-repl-", suffix=".log")
    p = subprocess.Popen([sys.executable, os.path.abspath(__file__), PROTOST, "--inner", log],
                         creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        rc = p.wait(timeout=180)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = 1
    try:
        # As bytes: the report is UTF-8, whatever this console's code page.
        with open(log, "rb") as f:
            sys.stdout.flush()
            sys.stdout.buffer.write(f.read())
            sys.stdout.flush()
    except OSError:
        sys.stdout.write("FAIL: the console session wrote no report\n")
        rc = rc or 1
    sys.exit(rc)


def run_windows_inner(log):
    import ctypes
    from ctypes import wintypes as wt
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    u32 = ctypes.WinDLL("user32", use_last_error=True)
    sys.stdout = open(log, "w", encoding="utf-8", buffering=1)

    class KEY_EVENT_RECORD(ctypes.Structure):
        _fields_ = [("bKeyDown", wt.BOOL), ("wRepeatCount", wt.WORD), ("wVirtualKeyCode", wt.WORD),
                    ("wVirtualScanCode", wt.WORD), ("uChar", wt.WCHAR), ("dwControlKeyState", wt.DWORD)]

    class INPUT_RECORD(ctypes.Structure):
        class _U(ctypes.Union):
            _fields_ = [("KeyEvent", KEY_EVENT_RECORD), ("pad", ctypes.c_byte * 16)]
        _fields_ = [("EventType", wt.WORD), ("Event", _U)]

    k32.CreateFileW.restype = wt.HANDLE
    conin = k32.CreateFileW("CONIN$", 0xC0000000, 3, None, 3, 0, None)
    out_path = log + ".out"
    out = open(out_path, "wb")
    home = tempfile.mkdtemp(prefix="protost-repl-")
    proc = subprocess.Popen([PROTOST, "-i"], stdout=out, stderr=subprocess.STDOUT,
                            env=dict(os.environ, HOME=home, USERPROFILE=home))
    # This process ignores Ctrl-C from here on; protost, already started,
    # keeps the default (the flag is inherited only by later children).
    k32.SetConsoleCtrlHandler(None, True)
    hwnd = k32.GetConsoleWindow()
    u32.SetForegroundWindow(hwnd)

    class ConsoleSession(Session):
        def output(self):
            with open(out_path, "rb") as f:
                return f.read().decode("utf-8", "replace")

        def send(self, text):
            recs = (INPUT_RECORD * (2 * len(text)))()
            for i, ch in enumerate(text):
                for j, down in enumerate((1, 0)):
                    r = recs[2 * i + j]
                    r.EventType = 1  # KEY_EVENT
                    ke = r.Event.KeyEvent
                    ke.bKeyDown = down
                    ke.wRepeatCount = 1
                    ke.uChar = ch
                    ke.wVirtualKeyCode = 0x0D if ch == "\r" else 0
            n = wt.DWORD()
            if not k32.WriteConsoleInputW(conin, recs, len(recs), ctypes.byref(n)):
                fail("WriteConsoleInputW: error %d" % ctypes.get_last_error(), self.output())

        def ctrl_c(self):
            u32.SetForegroundWindow(hwnd)
            VK_CONTROL, VK_C, KEYUP = 0x11, 0x43, 0x0002
            u32.keybd_event(VK_CONTROL, 0x1D, 0, 0)
            u32.keybd_event(VK_C, 0x2E, 0, 0)
            u32.keybd_event(VK_C, 0x2E, KEYUP, 0)
            u32.keybd_event(VK_CONTROL, 0x1D, KEYUP, 0)

        def exited(self):
            return proc.poll()

    s = ConsoleSession()
    rc = s.scenario()
    if rc is None:
        proc.kill()
        fail("a second Ctrl-C while blocked did not end the process", s.output())
    if (rc & 0xFFFFFFFF) != STATUS_CONTROL_C_EXIT:
        fail("exit status 0x%08X, not STATUS_CONTROL_C_EXIT" % (rc & 0xFFFFFFFF), s.output())
    print("OK")


if __name__ == "__main__":
    if not PROTOST:
        sys.exit(__doc__)
    if os.name == "nt":
        if len(sys.argv) > 3 and sys.argv[2] == "--inner":
            try:
                run_windows_inner(sys.argv[3])
            except SystemExit:
                raise
            except BaseException as e:  # report, never hang the outer process
                print("FAIL: %r" % (e,))
                sys.exit(1)
        else:
            run_windows_outer()
    else:
        run_posix()
