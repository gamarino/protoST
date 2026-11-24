#pragma once
// The generated constructor of a class with constants, and the
// end-of-construction check (explicit-state spec §6.2; plan decisions D-d,
// D-e as amended by the Task 6 rulings; messages M9 and M13).
//
// A class that declares or inherits a constant answers a class-side keyword
// constructor over all its constants, inherited first (`Account
// number:owner:`). The constructor is real bytecode, built here when the
// declaration runs, and installed on the class's metaclass directly:
//
//   PUSH_SELF                                    receiver of the check
//   PUSH_SELF; PUSH_LOCAL 1..n; MAKE_ARRAY n; PUSH_CONST #number:owner:
//   SEND_KEYWORD #__constructConstants:selector:   the instance, constants set
//   DUP; SEND_UNARY #initialize; POP              initialize, in this engine
//   PUSH_CONST #number:owner:
//   SEND_KEYWORD #__checkConstantsOf:after:      M13; the instance, or nil
//   RETURN
//
// (pre-flight ruling C11: local 0 is self, the arguments are locals 1..n).
// `initialize` is sent by the body itself, as `new` sends it, so it runs in
// the caller's engine: a Future wait in it behaves as under `new`. A
// primitive alone cannot know the selector it was sent under, so the body
// passes it.
//
// Where the constructor lives. A class owns a constructor only when the
// chain would not answer the right one otherwise: when it declares constants
// of its own, or when its parents' constructors do not cover its constants
// (two mixins with constants). Every other class inherits its parent's
// constructor and record through metaclass lookup, so nothing can go stale
// on it. The class that owns one records its selector under
// `__constant_constructor__`; `__installClassMethod:as:` refuses a method
// under the selector the chain records (M9), as the compiler does for a class
// it can see.
//
// A class that owns a constructor can still be left with a stale one, when a
// class it inherits from is redeclared in place with other constants: the
// runtime cannot enumerate subclasses to regenerate theirs. The constructor
// detects it when it is called, and signals an Error naming the class to
// redeclare.
//
// The constants a construction checks, and the constructors that are still
// valid for a class, come from a per-class cache keyed to the instance-layout
// epoch (InstanceTemplate.h): no chain walk per construction.

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

// M9 before a declaration changes anything (Task 6 ruling I1): the
// constructor `cls` would answer once its own constants are `ownConstants`
// must not take a selector its metaclass already holds for a user method.
// Throws a ClassedErrorSignal (Error) with M9's text when it would.
void checkConstantConstructorFree(STRuntime& rt, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls,
                                  const std::vector<std::string>& ownConstants);

// Installs, re-installs or removes the generated constructor of `cls` (a
// class, not a metaclass) for its recorded constants, as described above.
// Checks M9 first (checkConstantConstructorFree), so a refused class is left
// as it was. A constructor `cls` recorded earlier under another selector is
// removed from its metaclass. The module is handed to the runtime
// (STRuntime::adoptModule), which keeps it for its lifetime.
void installConstantConstructor(STRuntime& rt, proto::ProtoContext* ctx,
                                const proto::ProtoObject* cls);

// The selector `cls` answers as its generated constructor, read through the
// chain (pre-flight ruling C9), or "" when it has none.
std::string constantConstructorOf(STRuntime& rt, proto::ProtoContext* ctx,
                                  const proto::ProtoObject* cls);

// `cls __constructConstants: args selector: selector`: a new instance of
// `cls` with the non-nil values of `args` written in one write under the
// constants the selector names. A constructor that is no longer valid for
// `cls` signals an Error in place and answers nil.
const proto::ProtoObject* constructWithConstants(STRuntime& rt, proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* cls,
                                                 const proto::ProtoObject* args,
                                                 const proto::ProtoObject* selector);

// Whether `cls` declares or inherits a constant: the one test `new` and the
// kernel class-side constructors make (plan decision D-e). The answer comes
// from `__has_constants__` read through the chain, and is cached on the class
// itself under `__has_constants_verdict__` for the current instance-layout
// epoch, as the constant cache and the instance template are: every
// declaration of instance variables and every addBehavior: advances the
// epoch, so a class without constants pays one own read per construction and
// a cached answer never outlives a change of its chain. The verdict is a
// SmallInteger (epoch * 2 + answer), so one publication sets both and the GC
// has nothing to trace.
bool classHasConstants(STRuntime& rt, proto::ProtoContext* ctx, const proto::ProtoObject* cls);

// The end-of-construction check (M13): every constant of `cls` must be set on
// `instance`, its own attribute neither absent nor nil. The first unset one,
// in constantNamesOf order, signals an Error in place naming `selector`, and
// the construction answers nothing (nil). Answers `instance` when every
// constant is set, and nil for a nil instance (a construction that already
// signalled).
const proto::ProtoObject* checkConstantsSet(STRuntime& rt, proto::ProtoContext* ctx,
                                            const proto::ProtoObject* cls,
                                            const proto::ProtoObject* instance,
                                            const std::string& selector);

} // namespace protoST
