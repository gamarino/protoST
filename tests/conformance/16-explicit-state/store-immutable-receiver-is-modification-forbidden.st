"EXPECT: #(#('ModificationForbidden' 'cannot assign to the instance variable ''x'' of a BlockClosure: the receiver is immutable; nothing was written') #('ModificationForbidden' 'cannot assign to the instance variable ''x'' of a BlockClosure: the receiver is immutable; nothing was written') nil nil 2)"
"Spec §13.1 and §12, conformance 82: an instance-variable store whose
 receiver is an immutable value signals ModificationForbidden in place, so
 on: ModificationForbidden do: catches it by class. protoST 0.16.1 raised it
 as a NativeError, which no on:do: could select by class.

 The route (pre-flight ruling C17). The plan's route, BlockClosure
 addBehavior: M, does not reach a block: addBehavior: builds a new class
 object and rebinds the global, while closures keep the old chain, so
 [] setX is doesNotUnderstand: (checked on 0.16.1). The compiler refuses an
 instance-variable assignment in a BlockClosure method. So a real block made
 in a method of M, whose argument stands for the method's self slot, is
 installed as a BlockClosure method with the private __installMethod:as:.
 Sent to a block, it runs with self being that block, an immutable value.
 setX reaches STORE_INSTVAR; setXY assigns a run of two variables, which
 reaches IVAR_GROUP and IVAR_GROUP_END. Neither writes anything."
Object subclass: #M instanceVariableNames: 'x y'.
M >> grabOne
  ^ [:ignored | x := 1]
M >> grabRun
  ^ [:ignored | x := 1. y := 2. nil]

| caught target |
BlockClosure __installMethod: M new grabOne as: #setX.
BlockClosure __installMethod: M new grabRun as: #setXY.
caught := [:block | block on: ModificationForbidden do: [:e | {e class name. e messageText}]].
target := [:a | a + 1].
{caught value: [target setX].
 caught value: [target setXY].
 target instVarNamed: 'x'.
 target instVarNamed: 'y'.
 target value: 1}
