#!/usr/bin/env bash
#
# protoST conformance-suite runner.
#
# Black-box: runs each `.st` program through the `protost` binary in its own
# process, captures stdout/stderr and the exit code, and compares the result
# against a directive comment embedded in the file.
#
# Usage:
#   run_conformance.sh <path-to-protost> <test-file.st>
#
# Directive format (the directive MUST be the first line of the file, a
# protoST comment beginning immediately after the opening double-quote):
#
#   "EXPECT: <text>"        the program must succeed (exit 0) and the last
#                           line of stdout must equal <text> exactly.
#
#   "EXPECT-ERROR"          the program must fail (non-zero exit) — i.e. it
#   "EXPECT-ERROR: <text>"  raises an uncaught error. With a <text> argument
#                           the error output must contain <text>.
#
#   "XFAIL: <text>"         a test that is EXPECTED TO FAIL today (a known
#   "XFAIL-ERROR..."        deviation, or a newly-found discrepancy). The body
#                           after `XFAIL:` is itself an EXPECT:/EXPECT-ERROR
#                           directive describing the SPEC-CORRECT behaviour.
#                           The runner inverts the verdict: the test "passes"
#                           (exit 0) when the program does NOT match the
#                           spec-correct expectation, and FAILS loudly if it
#                           unexpectedly matches (the deviation got fixed —
#                           the XFAIL marker should then be removed).
#
# Exactly one CTest case is registered per file, so one failure pins one
# non-conforming program.

set -u

PROTOST="${1:?usage: run_conformance.sh <protost> <file.st>}"
FILE="${2:?usage: run_conformance.sh <protost> <file.st>}"

if [[ ! -x "$PROTOST" ]]; then
    echo "FAIL: protost binary not executable: $PROTOST"
    exit 1
fi
if [[ ! -f "$FILE" ]]; then
    echo "FAIL: test file not found: $FILE"
    exit 1
fi

# Resolve to absolute paths: the program is executed with its own directory
# as the working directory, so Import from: '<bare-name>' resolves to a
# sibling module file.
case "$PROTOST" in /*) ;; *) PROTOST="$(cd "$(dirname "$PROTOST")" && pwd)/$(basename "$PROTOST")" ;; esac
TEST_DIR="$(cd "$(dirname "$FILE")" && pwd)"
TEST_BASE="$(basename "$FILE")"

# --- read the directive (first line) -------------------------------------
directive_line="$(head -n 1 "$FILE")"
# strip a leading/trailing double-quote and surrounding whitespace
directive="${directive_line#\"}"
directive="${directive%\"}"
directive="${directive#"${directive%%[![:space:]]*}"}"   # ltrim
directive="${directive%"${directive##*[![:space:]]}"}"    # rtrim

xfail=0
if [[ "$directive" == XFAIL:* ]]; then
    xfail=1
    directive="${directive#XFAIL:}"
    directive="${directive#"${directive%%[![:space:]]*}"}"  # ltrim again
fi

mode=""
expected=""
case "$directive" in
    EXPECT-ERROR:*)
        mode="error"
        expected="${directive#EXPECT-ERROR:}"
        expected="${expected#"${expected%%[![:space:]]*}"}"
        ;;
    EXPECT-ERROR)
        mode="error"
        expected=""
        ;;
    EXPECT:*)
        mode="value"
        expected="${directive#EXPECT:}"
        expected="${expected#"${expected%%[![:space:]]*}"}"
        ;;
    *)
        echo "FAIL: $FILE has no valid directive on its first line"
        echo "  first line was: $directive_line"
        exit 1
        ;;
esac

# --- run the program (with the test's directory as the working dir) ------
out="$(cd "$TEST_DIR" && "$PROTOST" "$TEST_BASE" 2>/tmp/conf_err.$$)"
rc=$?
err="$(cat /tmp/conf_err.$$ 2>/dev/null)"
rm -f /tmp/conf_err.$$

# last non-empty line of stdout (the printed program value)
last_line="$(printf '%s\n' "$out" | grep -v '^[[:space:]]*$' | tail -n 1)"

# --- evaluate: did the program match the (spec-correct) expectation? ------
conforms=0
detail=""
if [[ "$mode" == "value" ]]; then
    if [[ $rc -eq 0 && "$last_line" == "$expected" ]]; then
        conforms=1
    else
        detail="expected value [$expected], got exit=$rc last-line=[$last_line] stderr=[$err]"
    fi
else  # mode == error
    if [[ $rc -ne 0 ]]; then
        if [[ -z "$expected" || "$err" == *"$expected"* || "$out" == *"$expected"* ]]; then
            conforms=1
        else
            detail="expected error containing [$expected], got exit=$rc stdout=[$out] stderr=[$err]"
        fi
    else
        detail="expected an error, but program succeeded with exit=0 last-line=[$last_line]"
    fi
fi

# --- verdict --------------------------------------------------------------
if [[ $xfail -eq 1 ]]; then
    if [[ $conforms -eq 1 ]]; then
        echo "XPASS: $FILE — marked XFAIL but the program now matches the spec."
        echo "  The deviation appears to be fixed; remove the XFAIL: marker."
        exit 1
    fi
    echo "XFAIL (as expected): $FILE"
    echo "  spec-correct behaviour not met — $detail"
    exit 0
else
    if [[ $conforms -eq 1 ]]; then
        echo "PASS: $FILE"
        exit 0
    fi
    echo "FAIL: $FILE — $detail"
    exit 1
fi
