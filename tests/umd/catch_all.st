"EXPECT: #('NegativeRadius' 'circleArea: the radius -1.000000 is negative' 42 'a plain object' 'Error' 'Exception' 'set' true 'outer Exception')"
"`on: Exception do:` is protoST's one catch-all form (LANGUAGE.md §8.7): it
 catches every signalled value -- a protoST exception, a module's error type,
 a value raised by another language even when it is not an exception at all
 (a SmallInteger, a plain object). `on: Error do:` catches protoST errors and
 protoCore's errors only. Run through protost_umd_host (umd_host.cpp)."
Geo := Import from: 'example/geometry'.
Raising := Import from: 'test/raising'.

{ [Geo circleArea(-1)] on: Exception do: [:e | e name].
  [Geo circleArea(-1)] on: Exception do: [:e | e messageText].
  [Raising raiseInteger()] on: Exception do: [:e | e].
  [Raising raiseObject()] on: Exception do: [:e | e messageText].
  "protoST's own errors and protoCore's errors are still Errors"
  [Error signal: 'x'] on: Exception do: [:e | 'Error'].
  [[Raising raiseObject()] on: Error do: [:e | 'wrong']] on: Exception do: [:e | 'Exception'].
  "Exception as a member of an ExceptionSet"
  [Raising raiseInteger()] on: ZeroDivide, Exception do: [:e | 'set'].
  "a raised non-exception value is not resumable"
  [[Raising raiseInteger()] on: Exception do: [:e | e resume: 1]]
     on: Error do: [:e | e messageText = 'cannot resume a non-resumable exception'].
  "pass from a specific handler reaches the catch-all"
  [[Geo circleArea(-1)] on: Geo NegativeRadius do: [:e | e pass]]
     on: Exception do: [:e | 'outer Exception'] }
