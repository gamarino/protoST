#!/usr/bin/env bash
# F8-2: DAP debug adapter handshake (protost --dap).
# Pipes a framed `initialize` request followed by a framed `disconnect`
# request into the adapter and asserts on the framed DAP output.
# stdout IS the DAP channel, so all assertions inspect captured stdout.
set -euo pipefail
PROTOST="$1"

# Builds a DAP-framed message: "Content-Length: N\r\n\r\n<body>".
# The byte count must be the exact body length.
framed() {
    local body="$1"
    printf 'Content-Length: %d\r\n\r\n%s' "${#body}" "$body"
}

INIT='{"seq":1,"type":"request","command":"initialize","arguments":{}}'
DISCONNECT='{"seq":2,"type":"request","command":"disconnect","arguments":{}}'

out=$( { framed "$INIT"; framed "$DISCONNECT"; } | "$PROTOST" --dap 2>/dev/null )

# --- 1. an initialize response with success:true is emitted -------------------
echo "$out" | grep -q '"command":"initialize"' \
    || { echo "FAIL: no initialize response"; echo "$out"; exit 1; }
echo "$out" | grep -q '"type":"response"' \
    || { echo "FAIL: no response message"; echo "$out"; exit 1; }
echo "$out" | grep -q '"success":true' \
    || { echo "FAIL: initialize not success:true"; echo "$out"; exit 1; }
echo "$out" | grep -q '"request_seq":1' \
    || { echo "FAIL: initialize response missing request_seq:1"; echo "$out"; exit 1; }

# --- 2. capabilities advertise supportsConfigurationDoneRequest ---------------
echo "$out" | grep -q '"supportsConfigurationDoneRequest":true' \
    || { echo "FAIL: capabilities missing supportsConfigurationDoneRequest"; echo "$out"; exit 1; }

# --- 3. an `initialized` event follows ----------------------------------------
echo "$out" | grep -q '"event":"initialized"' \
    || { echo "FAIL: no initialized event"; echo "$out"; exit 1; }

# --- 4. disconnect is answered ------------------------------------------------
echo "$out" | grep -q '"command":"disconnect"' \
    || { echo "FAIL: no disconnect response"; echo "$out"; exit 1; }

# --- 5. output is Content-Length framed --------------------------------------
echo "$out" | grep -q 'Content-Length: ' \
    || { echo "FAIL: output is not Content-Length framed"; echo "$out"; exit 1; }

# --- 6. adapter exits 0 on disconnect ----------------------------------------
{ framed "$INIT"; framed "$DISCONNECT"; } | "$PROTOST" --dap >/dev/null 2>&1 \
    || { echo "FAIL: --dap exited non-zero on disconnect"; exit 1; }

# --- 7. an unknown command gets a benign success stub ------------------------
UNKNOWN='{"seq":1,"type":"request","command":"someFutureCommand","arguments":{}}'
out2=$( { framed "$UNKNOWN"; framed "$DISCONNECT"; } | "$PROTOST" --dap 2>/dev/null )
echo "$out2" | grep -q '"command":"someFutureCommand"' \
    || { echo "FAIL: unknown command got no response"; echo "$out2"; exit 1; }

echo OK
