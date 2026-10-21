"EXPECT: #('circleArea: the radius -1.000000 is negative' 'NegativeRadius' 'passed the unrelated handler' 'not an Error' 'triangleArea: the sides do not make a triangle' 'InvalidArgument' false 'ZeroDivide' 'legacyArea: negative radius' 7 0.0 'cannot resume a non-resumable exception' 'outer' 'set' true true 'circleArea: the radius -1.000000 is negative')"
"A module written in C++ -- protoCore's example geometry module, a UMD
 provider in its own shared library -- defines its own error types and raises
 them as proto::ProtoExceptions. A protoST handler names the module's type and
 selects the error by its prototype chain; protoCore's own errors arrive as
 protoST classes; a C++ std::exception arrives as a plain Error. Run through
 protost_umd_host (umd_host.cpp), which registers the provider."
Geo := Import from: 'example/geometry'.

Object subclass: #Measurer.
Measurer >> area: r in: geometry
  ^ geometry circleArea(r).

attempts := 0.
cleaned := false.
[[Geo circleArea(-1)] ensure: [cleaned := true]] on: Geo GeometryError do: [:e | nil].

{ "caught by the module's own type, with the module's message"
  [Geo circleArea(-1)] on: Geo NegativeRadius do: [:e | e messageText].
  "caught by the module's base type: the handler receives the raised value"
  [Geo circleArea(-1)] on: Geo GeometryError do: [:e | e name].
  "a handler for an unrelated type does not run"
  [[Geo circleArea(-1)] on: Geo DegenerateTriangle do: [:e | 'wrong']]
     on: Geo GeometryError do: [:e | 'passed the unrelated handler'].
  "a module's error type is not a subclass of Error"
  [[Geo circleArea(-1)] on: Error do: [:e | 'Error']]
     on: Geo GeometryError do: [:e | 'not an Error'].
  [Geo triangleArea(1, 2, 10)] on: Geo DegenerateTriangle do: [:e | e messageText].
  "protoCore's malformed-literal error, raised inside the module, built by the
   runtime's error factory"
  [Geo parseRadius('ten')] on: InvalidArgument do: [:e | e class name].
  [Geo parseRadius('ten')] on: Error do: [:e | e isResumable].
  [10 // 0] on: ZeroDivide do: [:e | e class name].
  "a C++ std::runtime_error is a module defect: a plain Error with what()"
  [Geo legacyArea(-1)] on: Error do: [:e | e messageText].
  "the handler actions answer for a foreign exception"
  [Geo circleArea(-1). 'continued'] on: Geo GeometryError do: [:e | e return: 7].
  [attempts := attempts + 1. Geo circleArea(attempts - 2)]
     on: Geo NegativeRadius do: [:e | e retry].
  [[Geo circleArea(-1)] on: Geo GeometryError do: [:e | e resume: 1]]
     on: Error do: [:e | e messageText].
  [[Geo circleArea(-1)] on: Geo NegativeRadius do: [:e | e pass]]
     on: Geo GeometryError do: [:e | 'outer'].
  [Geo circleArea(-1)] on: ZeroDivide, Geo GeometryError do: [:e | 'set'].
  "ensure: ran while the module's error unwound"
  cleaned.
  (Geo circleArea(2)) > 12.
  "an actor method's unhandled module error rejects its Future with the same
   value, and wait re-signals it in the waiter"
  [(Measurer new asActor area: -1 in: Geo) wait] on: Geo NegativeRadius do: [:e | e messageText] }
