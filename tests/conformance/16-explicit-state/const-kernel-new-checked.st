"EXPECT: #('the constant ''a'' of an OC is not set at the end of construction (new)' 'the constant ''a'' of a MyErr is not set at the end of construction (new)' 'the constant ''a'' of a Dic is not set at the end of construction (new)' 'the constant ''a'' of a Sorted is not set at the end of construction (new)' 'the constant ''a'' of a Writer is not set at the end of construction (new)' 'OC' 1 'ok')"
"Spec §6.2, M13, Task 6 ruling on kernel new: every kernel class-side new
 that builds an instance of a user subclass runs the end-of-construction
 check, whether it is a primitive (OrderedCollection, Dictionary) or a
 kernel method that never reaches Object class >> new (SortedCollection,
 WriteStream). An initialize that sets the constant passes."
OrderedCollection subclass: #OC instanceVariableNames: '' constantNames: 'a'.
Error subclass: #MyErr instanceVariableNames: '' constantNames: 'a'.
Dictionary subclass: #Dic instanceVariableNames: '' constantNames: 'a'.
SortedCollection subclass: #Sorted instanceVariableNames: '' constantNames: 'a'.
WriteStream subclass: #Writer instanceVariableNames: '' constantNames: 'a'.
OrderedCollection subclass: #Filled instanceVariableNames: '' constantNames: 'a'.
Filled >> initialize
  a := 1
Filled >> a
  ^ a

{[OC new. 'no M13'] on: Error do: [:e | e messageText].
 [MyErr new. 'no M13'] on: Error do: [:e | e messageText].
 [Dic new. 'no M13'] on: Error do: [:e | e messageText].
 [Sorted new. 'no M13'] on: Error do: [:e | e messageText].
 [Writer new. 'no M13'] on: Error do: [:e | e messageText].
 [(OC a: 1) class name] on: Error do: [:e | e messageText].
 Filled new a.
 [(Filled withAll: #(1 2)) size = 2 ifTrue: ['ok']] on: Error do: [:e | e messageText]}
