#pragma once
// The generated constructor of a class with constants, and the
// end-of-construction check (explicit-state spec §6.2; plan decisions D-d,
// D-e; messages M9 and M13).
//
// A class that declares or inherits a constant gets a class-side keyword
// constructor over all its constants, inherited first (`Account
// number:owner:`). The constructor is real bytecode, built here when the
// declaration runs, and installed on the class's metaclass directly:
//
//   PUSH_SELF; PUSH_LOCAL 1..n; MAKE_ARRAY n; PUSH_CONST #number:owner:;
//   SEND_KEYWORD #__constructConstants:selector:; RETURN
//
// (pre-flight ruling C11: local 0 is self, the arguments are locals 1..n).
// The primitive `__constructConstants:selector:` makes the instance, writes
// the non-nil arguments in one write, sends `initialize`, and checks that
// every constant is set (M13). A primitive alone cannot know the selector it
// was sent under, so each class carries its own small method that passes it.
//
// The class records the constructor's selector under
// `__constant_constructor__`; `__installClassMethod:as:` refuses a method
// under it (M9), as the compiler does for a class it can see.

#include <string>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoObject;
class ProtoString;
} // namespace proto

namespace protoST {

class STRuntime;

// The key of the class record holding the generated constructor's selector.
const proto::ProtoString* constantConstructorKey(proto::ProtoContext* ctx);

// Installs, or re-installs, the generated constructor of `cls` (a class, not a
// metaclass) over constantNamesOf(cls). A class without constants gets none,
// and loses one it had. A constructor recorded earlier under another selector
// (the class was redeclared, or rebuilt by addBehavior:) is removed from the
// metaclass. The module is handed to the runtime (STRuntime::adoptModule),
// which keeps it for its lifetime.
void installConstantConstructor(STRuntime& rt, proto::ProtoContext* ctx,
                                const proto::ProtoObject* cls);

// The selector `cls` records as its generated constructor, read through the
// chain (pre-flight ruling C9), or "" when it has none.
std::string constantConstructorOf(STRuntime& rt, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls);

// The work of `cls __constructConstants: anArray selector: aSymbol`: a new
// instance of `cls`, the non-nil values of `args` written in one write under
// the constants the selector's keywords name, `initialize`, then the
// end-of-construction check naming `selector`. Answers the instance, or what
// the M13 signal answers.
const proto::ProtoObject* constructWithConstants(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* cls,
                                                 const proto::ProtoObject* args,
                                                 const std::string& selector);

// The end-of-construction check (M13): every constant of `cls`
// (constantNamesOf order) must be set on `instance`, its own attribute neither
// absent nor nil. Signals an Error in place for the first unset one, naming
// `selector`, and answers what the signal answers; answers `instance` when
// every constant is set.
const proto::ProtoObject* checkConstantsSet(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* cls,
                                            const proto::ProtoObject* instance,
                                            const std::string& selector);

} // namespace protoST
