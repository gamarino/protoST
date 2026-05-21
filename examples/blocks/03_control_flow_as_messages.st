"EXPECT: fizzbuzz-result-240"
"-- 03_control_flow_as_messages.st -----------------------------------------"
"-- protoST has no built-in control flow: conditionals and loops are        --"
"-- ordinary message sends taking blocks. This FizzBuzz over 1..30 uses     --"
"-- `to:do:` for the loop and `ifTrue:ifFalse:` for the branching, then     --"
"-- sums every numeric (non-Fizz/Buzz) value as a deterministic check.      --"
"-- Expected output: fizzbuzz-result-810                                    --"

sum := 0.
1 to: 30 do: [ :n |
  ((n \\ 15) = 0)
    ifTrue:  [ "FizzBuzz — contributes nothing" nil ]
    ifFalse: [
      ((n \\ 3) = 0)
        ifTrue:  [ "Fizz" nil ]
        ifFalse: [
          ((n \\ 5) = 0)
            ifTrue:  [ "Buzz" nil ]
            ifFalse: [ sum := sum + n ] ] ] ].

"Plain numbers in 1..30 sum to 240 once multiples of 3 and 5 are removed."
'fizzbuzz-result-' , sum printString.
