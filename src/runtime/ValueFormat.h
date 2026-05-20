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

} // namespace protoST
