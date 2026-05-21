#pragma once
#include <string>

namespace proto { class ProtoContext; class ProtoObject; }

namespace protoST {

class STRuntime;

// BL-3: single shared ProtoObject -> display-string helper.
//
// Replaces the format block that was duplicated across `protost -e`, the
// REPL, the text debugger and the DAP adapter. Logic:
//   * nullptr / PROTO_NONE -> "nil", PROTO_TRUE -> "true", PROTO_FALSE -> "false"
//   * integer              -> its decimal digits
//   * string               -> the string contents (unquoted, matching the
//                              prior REPL / -e behaviour)
//   * any other object     -> the default `printString` rendering, i.e.
//                              "a ClassName" / "an ClassName" resolved by
//                              reading the `__class_name__` attribute walked
//                              up the prototype chain. A class object renders
//                              as its bare name. Falls back to "an object".
//
// The non-primitive case is resolved purely in C++ (no bytecode is run), so
// formatValue is safe to call from the debugger / DAP while the engine is
// stopped — there is no re-entrancy risk. A user-defined `printString`
// override is therefore NOT consulted by this helper; the REPL / -e paths can
// reach an override through ordinary evaluation if desired.
std::string formatValue(STRuntime& rt, proto::ProtoContext* ctx,
                        const proto::ProtoObject* v);

// Numeric tower rendering (D11 / D20).
//
// protoCore stores numbers as tagged `SmallInteger`, heap `LargeInteger` and
// heap `Double`, but does NOT expose a number -> string conversion (its
// `asString` answers nil for a number). protoST therefore renders numbers
// itself:
//   * a Float          -> the shortest round-tripping decimal, always with a
//                         fractional part (`4.0`, not `4`)
//   * a SmallInteger    -> its decimal digits via `asLong`
//   * a LargeInteger    -> its exact decimal digits, extracted with repeated
//                         protoCore `divmod` by 10 (so a value far past the
//                         56-bit inline range still prints exactly)
//
// `isNumber` answers whether `v` is any number; `formatNumber` renders one
// (the caller must have established it is a number, e.g. via `isNumber`).
bool isNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v);
std::string formatNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v);

} // namespace protoST
