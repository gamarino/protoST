"EXPECT: rpn-result-20"
"-- rpn_interpreter.st -----------------------------------------------------"
"-- A complete small program: a tiny interpreter for a postfix (RPN)        --"
"-- stack language. A program is an Array of tokens — numbers push onto an  --"
"-- operand stack; an operator pops two operands and pushes the result.     --"
"--                                                                        --"
"-- The dispatch table is a Dictionary mapping each operator symbol to a    --"
"-- two-argument block, so the interpreter core is a single `do:` loop —    --"
"-- adding an operator is just adding a Dictionary entry, no new code.      --"
"--                                                                        --"
"-- Program  3 4 + 5 *  evaluates as  (3 + 4) * 5 = 35 ... here we run      --"
"-- 2 3 + 4 *  =  (2 + 3) * 4 = 20.                                          --"

Object subclass: #RPNMachine
  instanceVariableNames: 'stack ops'.

RPNMachine >> initialize
  stack := OrderedCollection new.
  "Each operator is a block taking the two popped operands (a then b)."
  ops := Dictionary new.
  ops at: #+ put: [ :a :b | a + b ].
  ops at: #- put: [ :a :b | a - b ].
  ops at: #* put: [ :a :b | a * b ].
  ops at: #/ put: [ :a :b | a / b ].
  ^ self.

RPNMachine >> isOperator: token
  ^ ops includesKey: token.

"Run a token program; return the single value left on the stack."
RPNMachine >> run: program
  program do: [ :token |
    (self isOperator: token)
      ifTrue: [
        | b a op |
        "Pop in reverse order: the last pushed operand is the right one."
        b := stack removeLast.
        a := stack removeLast.
        op := ops at: token.
        stack addLast: (op value: a value: b) ]
      ifFalse: [ stack addLast: token ] ].
  ^ stack last.

"-- driver: evaluate the RPN program  2 3 + 4 *  --"
machine := RPNMachine new.
machine initialize.
result := machine run: { 2. 3. #+. 4. #* }.

'rpn-result-' , result printString.
