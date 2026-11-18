#!/usr/bin/env bash
# protoST depends on protoCore's public API only (headers/protoCore.h): no
# runtime may include protoCore's internal header or hand-copy its pointer
# tags, embedded-type bits or cell layouts, which protoCore is free to change
# without notice. This check fails when such a dependency appears under the
# directories it is given (the runtime's src/ and include/).
#
# What it looks for:
#   - the internal header (proto_internal.h);
#   - protoCore's internal tag and type names (POINTER_TAG_*, EMBEDDED_TYPE_*,
#     CellType), and protoST-local copies of them (kTag...);
#   - a bit mask applied to a pointer reinterpreted as an integer, the shape
#     of a hand-copied tag test: `(reinterpret_cast<uintptr_t>(p) & 0x3F)`.
#     Hashing an address (a shift, then a mask) is not a tag test and passes.
# Use the ProtoObject is*/as* predicates, isSmallInt and isStringTagFast.
set -euo pipefail
[ $# -ge 1 ] || { echo "usage: $0 DIR..."; exit 2; }
pattern='proto_internal\.h|POINTER_TAG|EMBEDDED_TYPE|CellType|\bkTag[A-Za-z_]*|u?intptr_t>\([^()]*\)\)*[[:space:]]*&[^&]'
hits=$(grep -rnE --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.cc' \
       "$pattern" "$@" || true)
if [ -n "$hits" ]; then
    echo "FAIL: protoCore internals used outside its public API:"
    echo "$hits"
    exit 1
fi
echo OK
