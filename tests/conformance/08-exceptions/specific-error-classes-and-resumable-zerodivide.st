"EXPECT: #(0 'ArithmeticError' true 'SubscriptOutOfBounds' 'SubscriptOutOfBounds' 'KeyNotFound' 'NotFound' true false 42 'caught as Error')"
"ZeroDivide is a resumable ArithmeticError; indexing out of bounds, a missing
 key and removing an absent element signal SubscriptOutOfBounds, KeyNotFound
 and NotFound (all Errors); isResumable and retryUsing: are available."
{ [1/0] on: ZeroDivide do: [:e | e resume: 0].
  ZeroDivide superclass name.
  [1/0] on: ZeroDivide do: [:e | e isResumable].
  [#(1 2) at: 5] on: SubscriptOutOfBounds do: [:e | e class name].
  ['ab' at: 9] on: SubscriptOutOfBounds do: [:e | e class name].
  [Dictionary new at: #missing] on: KeyNotFound do: [:e | e class name].
  [OrderedCollection new remove: 3] on: NotFound do: [:e | e class name].
  [Warning signal: 'w'] on: Warning do: [:e | e isResumable].
  [Error signal: 'x'] on: Error do: [:e | e isResumable].
  [Error signal: 'x'] on: Error do: [:e | e retryUsing: [42]].
  [#() first] on: Error do: [:e | 'caught as Error'] }
