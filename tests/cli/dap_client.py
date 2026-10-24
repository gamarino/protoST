"""A small Debug Adapter Protocol client for the protoST DAP tests.

Spawns `protost --dap`, reads its framed messages on a background thread and
lets a test send requests, wait for their responses and wait for events. Every
wait has a deadline, so a broken adapter makes the test fail instead of hang.
"""
import json
import queue
import subprocess
import threading
import time


class DapError(Exception):
    pass


class DapClient:
    def __init__(self, protost, env=None):
        # `protost` is the executable, or an argv prefix that runs it
        # elsewhere (["wsl", "-d", "Ubuntu-24.04", "--", "/path/protost"]).
        argv = list(protost) if isinstance(protost, (list, tuple)) else [protost]
        self.p = subprocess.Popen(argv + ["--dap"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  env=env)
        self.seq = 1
        self.responses = {}
        self.events = []          # every event, in arrival order
        self.output = []          # (category, text) of output events
        self.cv = threading.Condition()
        self.closed = False
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    # --- transport ---------------------------------------------------------
    def _read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.p.stdout.read(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _read_loop(self):
        try:
            while True:
                header = b""
                while not header.endswith(b"\r\n\r\n"):
                    c = self.p.stdout.read(1)
                    if not c:
                        return
                    header += c
                n = 0
                for h in header.decode().split("\r\n"):
                    if h.lower().startswith("content-length"):
                        n = int(h.split(":")[1])
                body = self._read_exact(n)
                if body is None:
                    return
                m = json.loads(body.decode("utf-8"))
                with self.cv:
                    if m.get("type") == "response":
                        self.responses[m.get("request_seq")] = m
                    elif m.get("type") == "event":
                        self.events.append(m)
                        if m.get("event") == "output":
                            b = m.get("body", {})
                            self.output.append((b.get("category"), b.get("output", "")))
                    self.cv.notify_all()
        finally:
            with self.cv:
                self.closed = True
                self.cv.notify_all()

    def send(self, command, args=None):
        seq = self.seq
        self.seq += 1
        body = json.dumps({"seq": seq, "type": "request", "command": command,
                           "arguments": args or {}}).encode()
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n%s" % (len(body), body))
        self.p.stdin.flush()
        return seq

    def wait_response(self, seq, timeout=20):
        deadline = time.time() + timeout
        with self.cv:
            while seq not in self.responses:
                left = deadline - time.time()
                if left <= 0 or self.closed:
                    raise DapError("no response to request %d" % seq)
                self.cv.wait(left)
            return self.responses.pop(seq)

    def request(self, command, args=None, timeout=20):
        return self.wait_response(self.send(command, args), timeout)

    def wait_event(self, pred, timeout=20, start=0):
        """The first event at index >= start that satisfies pred, and its index."""
        deadline = time.time() + timeout
        with self.cv:
            i = start
            while True:
                while i < len(self.events):
                    if pred(self.events[i]):
                        return self.events[i], i
                    i += 1
                left = deadline - time.time()
                if left <= 0 or self.closed:
                    raise DapError("event not seen")
                self.cv.wait(left)

    def stopped(self, timeout=20, start=0, thread=None):
        return self.wait_event(
            lambda e: e.get("event") == "stopped" and
            (thread is None or e["body"].get("threadId") == thread),
            timeout, start)

    def output_text(self, category="stdout"):
        with self.cv:
            return "".join(t for c, t in self.output if c == category)

    # --- conveniences --------------------------------------------------------
    def start(self, program, breakpoints=(), exception_filters=None, stop_on_entry=False):
        self.request("initialize", {"adapterID": "protost"})
        self.request("launch", {"program": program, "stopOnEntry": stop_on_entry})
        if exception_filters is not None:
            self.request("setExceptionBreakpoints", {"filters": list(exception_filters)})
        self.request("setBreakpoints", {"source": {"path": program},
                                        "breakpoints": [{"line": l} for l in breakpoints]})
        self.request("configurationDone")

    def top_frame(self, thread):
        st = self.request("stackTrace", {"threadId": thread})
        frames = st["body"]["stackFrames"]
        return frames

    def locals(self, frame_id):
        sc = self.request("scopes", {"frameId": frame_id})
        ref = sc["body"]["scopes"][0]["variablesReference"]
        va = self.request("variables", {"variablesReference": ref})
        return {v["name"]: v["value"] for v in va["body"]["variables"]}

    def evaluate(self, expr, frame_id=None):
        args = {"expression": expr, "context": "repl"}
        if frame_id is not None:
            args["frameId"] = frame_id
        return self.request("evaluate", args)

    def finish(self, timeout=30):
        """Wait for terminated, disconnect, and return the exit code."""
        self.wait_event(lambda e: e.get("event") == "terminated", timeout)
        try:
            self.request("disconnect", timeout=10)
        except DapError:
            pass
        try:
            return self.p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.p.kill()
            return -1

    def kill(self):
        try:
            self.p.kill()
        except Exception:
            pass
