#!/usr/bin/env bash
# run.sh - thin wrapper around the protoST benchmark harness (Track 11).
#
# Runs the comparable-workload suite (protoST vs CPython, vs protopy when a
# built protopy binary is found) and the actor-model benchmarks, then writes a
# dated markdown report under benchmarks/reports/.
#
# Usage:
#   benchmarks/run.sh [extra args passed through to run_benchmarks.py]
#
# Honoured env vars: PROTOST_BIN, PROTOPY_BIN, CPYTHON_BIN (see run_benchmarks.py).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/run_benchmarks.py" "$@"
