"EXPECT: 14"
"-- calculator.st ----------------------------------------------------------"
"-- A complete small program: a recursive-descent calculator for           --"
"-- arithmetic expressions with +, -, *, / and parentheses, honouring       --"
"-- the usual precedence (* / bind tighter than + -).                       --"
"--                                                                        --"
"-- The expression is supplied as an Array of tokens (numbers, and the      --"
"-- one-character operator/paren strings). The parser is three mutually     --"
"-- recursive methods — expression, term, factor — over a token cursor.     --"
"-- Evaluating  '2 + 3 * 4'  gives 14.                                      --"

Object subclass: #Calculator instanceVariableNames: 'tokens pos'.

Calculator >> on: tokenArray
  tokens := tokenArray.
  pos := 1.
  ^ self.

"-- cursor helpers --"
Calculator >> atEnd
  ^ pos > tokens size.

Calculator >> peek
  ^ self atEnd ifTrue: [ nil ] ifFalse: [ tokens at: pos ].

Calculator >> advance
  | t |
  t := self peek.
  pos := pos + 1.
  ^ t.

"-- grammar: expression := term (('+' | '-') term)* --"
Calculator >> expression
  | value op |
  value := self term.
  [ (self peek = '+') or: [ self peek = '-' ] ] whileTrue: [
    op := self advance.
    op = '+'
      ifTrue:  [ value := value + self term ]
      ifFalse: [ value := value - self term ] ].
  ^ value.

"-- term := factor (('*' | '/') factor)* --"
Calculator >> term
  | value op |
  value := self factor.
  [ (self peek = '*') or: [ self peek = '/' ] ] whileTrue: [
    op := self advance.
    op = '*'
      ifTrue:  [ value := value * self factor ]
      ifFalse: [ value := value / self factor ] ].
  ^ value.

"-- factor := number | '(' expression ')' --"
Calculator >> factor
  | t |
  t := self advance.
  t = '('
    ifTrue: [
      | inner |
      inner := self expression.
      self advance.    "consume the matching ')'"
      ^ inner ].
  ^ t.

Calculator >> evaluate
  ^ self expression.

"-- driver: evaluate  ( 2 + 3 ) ... actually 2 + 3 * 4 = 14 --"
calc := Calculator new.
calc on: { 2. '+'. 3. '*'. 4 }.
calc evaluate.
