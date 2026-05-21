"-- json.st: the JSON standard-library module --

Parse and generate JSON. Pure protoST: no JSON-specific primitives — the
parser is a hand-written recursive-descent scanner over the input String.

USAGE
  m := Import from: 'json'.
  d := m JSON parse: aJsonObjectString.
  d at: 'name'.                          => 'Ada'
  s := m JSON stringify: d.              => a JSON String

MAPPING (parse: — JSON -> protoST)
  object {...}   -> a Dictionary  (String keys -> values)
  array  [...]   -> an Array
  string         -> a String
  number         -> a SmallInteger / LargeInteger when integral,
                     a Float when it has a fraction or exponent
  true / false   -> a Boolean
  null           -> nil

MAPPING (stringify: — protoST -> JSON)
  Dictionary                 -> {...}
  Array / OrderedCollection  -> [...]
  String                     -> a quoted, escaped JSON string
  integer / Float            -> a number
  Boolean                    -> true / false
  nil                        -> null

Invalid input (a syntax error or trailing junk) signals a catchable `Error`
whose messageText names the position and what was expected.

The scanner (the internal `_JSONScanner` class — prefixed `_` so it stays off
the module surface) holds the input String and a 1-based cursor; it offers
`peek`, `next`, `atEnd` and whitespace skipping. JSON characters are compared
through their Unicode code point (`aString asInteger`), since protoST has no
distinct Character type — a character is a 1-character String."

"=== _JSONScanner ==========================================================="

Object subclass: #_JSONScanner
  instanceVariableNames: 'text position limit'.

"Class-side constructor: a scanner positioned at the start of aString."
_JSONScanner class >> on: aString
  | s |
  s := self new.
  s setText: aString.
  ^ s.

"Private initialiser."
_JSONScanner >> setText: aString
  text := aString.
  position := 1.
  limit := aString size.

"True when the cursor is past the last character."
_JSONScanner >> atEnd
  ^ position > limit.

"The current character (a 1-char String) without advancing; nil at end."
_JSONScanner >> peek
  self atEnd ifTrue: [ ^ nil ].
  ^ text at: position.

"The current character, advancing the cursor. Signals an Error past the end."
_JSONScanner >> next
  | c |
  self atEnd ifTrue: [ ^ self signalError: 'unexpected end of input' ].
  c := text at: position.
  position := position + 1.
  ^ c.

"The current 1-based cursor position (for error messages)."
_JSONScanner >> position
  ^ position.

"Signal a catchable Error describing a syntax problem at the cursor."
_JSONScanner >> signalError: aMessage
  ^ Error signal: 'JSON parse error at position ' , position printString
                 , ': ' , aMessage.

"Advance past any run of JSON whitespace (space, tab, CR, LF)."
_JSONScanner >> skipWhitespace
  [ self atEnd not and: [ self _isWhitespace: self peek ] ]
    whileTrue: [ position := position + 1 ].

"True when aChar (a 1-char String) is JSON whitespace."
_JSONScanner >> _isWhitespace: aChar
  | code |
  code := aChar asInteger.
  ^ (code = 32) or: [ (code = 9) or: [ (code = 10) or: [ code = 13 ] ] ].

"Consume the literal aString or signal an Error (used for true/false/null)."
_JSONScanner >> expectLiteral: aString
  | n i |
  n := aString size.
  i := 1.
  [ i <= n ] whileTrue: [
    (self atEnd or: [ (self next) ~= (aString at: i) ])
      ifTrue: [ ^ self signalError: 'expected ' , aString ].
    i := i + 1 ].
  ^ true.

"=== JSON ==================================================================="

Object subclass: #JSON
  instanceVariableNames: ''.

"--- class-side entry points ---"

"Parse a JSON document into protoST objects. Signals a catchable Error on
 malformed input or trailing non-whitespace junk."
JSON class >> parse: aString
  | scanner value |
  scanner := _JSONScanner on: aString.
  scanner skipWhitespace.
  value := self parseValue: scanner.
  scanner skipWhitespace.
  scanner atEnd ifFalse: [
    ^ scanner signalError: 'unexpected trailing characters' ].
  ^ value.

"Render anObject as a JSON String — the inverse of parse:."
JSON class >> stringify: anObject
  ^ self stringifyValue: anObject.

"--- parsing: recursive descent ---"

"Parse a single JSON value at the scanner's cursor (whitespace already
 skipped). Dispatches on the first character."
JSON class >> parseValue: scanner
  | c code |
  c := scanner peek.
  c isNil ifTrue: [ ^ scanner signalError: 'expected a value' ].
  c = '{' ifTrue: [ ^ self parseObject: scanner ].
  c = '[' ifTrue: [ ^ self parseArray: scanner ].
  c = '"' ifTrue: [ ^ self parseString: scanner ].
  c = 't' ifTrue: [ scanner expectLiteral: 'true'.  ^ true ].
  c = 'f' ifTrue: [ scanner expectLiteral: 'false'. ^ false ].
  c = 'n' ifTrue: [ scanner expectLiteral: 'null'.  ^ nil ].
  code := c asInteger.
  ((code = 45) or: [ self isDigitCode: code ])
    ifTrue: [ ^ self parseNumber: scanner ].
  ^ scanner signalError: 'unexpected character ' , c.

"True when code is the code point of an ASCII digit 0..9."
JSON class >> isDigitCode: code
  ^ (code >= 48) and: [ code <= 57 ].

"Parse a JSON object {...} into a Dictionary."
JSON class >> parseObject: scanner
  | dict key value |
  scanner next.                     "consume '{'"
  dict := Dictionary new.
  scanner skipWhitespace.
  scanner peek = '}' ifTrue: [ scanner next. ^ dict ].
  [ true ] whileTrue: [
    scanner skipWhitespace.
    scanner peek = '"'
      ifFalse: [ ^ scanner signalError: 'expected a string key' ].
    key := self parseString: scanner.
    scanner skipWhitespace.
    scanner peek = ':'
      ifFalse: [ ^ scanner signalError: 'expected '':'' after key' ].
    scanner next.
    scanner skipWhitespace.
    value := self parseValue: scanner.
    dict at: key put: value.
    scanner skipWhitespace.
    scanner peek = ','
      ifTrue: [ scanner next ]
      ifFalse: [
        scanner peek = '}'
          ifTrue: [ scanner next. ^ dict ]
          ifFalse: [ ^ scanner signalError: 'expected '','' or ''}''' ] ] ].

"Parse a JSON array [...] into an Array."
JSON class >> parseArray: scanner
  | items |
  scanner next.                     "consume '['"
  items := OrderedCollection new.
  scanner skipWhitespace.
  scanner peek = ']' ifTrue: [ scanner next. ^ items asArray ].
  [ true ] whileTrue: [
    scanner skipWhitespace.
    items add: (self parseValue: scanner).
    scanner skipWhitespace.
    scanner peek = ','
      ifTrue: [ scanner next ]
      ifFalse: [
        scanner peek = ']'
          ifTrue: [ scanner next. ^ items asArray ]
          ifFalse: [ ^ scanner signalError: 'expected '','' or '']''' ] ] ].

"Parse a JSON string literal into a protoST String. The opening quote is at
 the cursor; honours the JSON backslash escapes."
JSON class >> parseString: scanner
  | result c code |
  scanner next.                     "consume the opening quote"
  result := ''.
  [ true ] whileTrue: [
    scanner atEnd ifTrue: [ ^ scanner signalError: 'unterminated string' ].
    c := scanner next.
    c = '"' ifTrue: [ ^ result ].
    c = '\'
      ifTrue: [ result := result , (self parseEscape: scanner) ]
      ifFalse: [
        code := c asInteger.
        code < 32
          ifTrue: [ ^ scanner signalError: 'control character in string' ].
        result := result , c ] ].

"Parse the body of a backslash escape (the backslash already consumed) and
 answer the unescaped character as a String."
JSON class >> parseEscape: scanner
  | e |
  scanner atEnd ifTrue: [ ^ scanner signalError: 'unterminated escape' ].
  e := scanner next.
  e = '"'  ifTrue: [ ^ '"' ].
  e = '\'  ifTrue: [ ^ '\' ].
  e = '/'  ifTrue: [ ^ '/' ].
  e = 'n'  ifTrue: [ ^ 10 asCharacter ].
  e = 't'  ifTrue: [ ^ 9 asCharacter ].
  e = 'r'  ifTrue: [ ^ 13 asCharacter ].
  e = 'b'  ifTrue: [ ^ 8 asCharacter ].
  e = 'f'  ifTrue: [ ^ 12 asCharacter ].
  e = 'u'  ifTrue: [ ^ self parseUnicodeEscape: scanner ].
  ^ scanner signalError: 'invalid escape \' , e.

"Parse the four hex digits of a \\uXXXX escape and answer the character."
JSON class >> parseUnicodeEscape: scanner
  | code i digit |
  code := 0.
  i := 1.
  [ i <= 4 ] whileTrue: [
    scanner atEnd ifTrue: [ ^ scanner signalError: 'truncated \\u escape' ].
    digit := self hexValue: scanner next forScanner: scanner.
    code := (code * 16) + digit.
    i := i + 1 ].
  ^ code asCharacter.

"The numeric value of a single hex digit (a 1-char String); signal otherwise."
JSON class >> hexValue: aChar forScanner: scanner
  | code |
  code := aChar asInteger.
  (code >= 48) and: [ code <= 57 ] ifTrue: [ ^ code - 48 ].
  (code >= 65) and: [ code <= 70 ] ifTrue: [ ^ code - 55 ].
  (code >= 97) and: [ code <= 102 ] ifTrue: [ ^ code - 87 ].
  ^ scanner signalError: 'invalid hex digit ' , aChar.

"Parse a JSON number. Builds the value digit by digit: an integral mantissa
 with no fraction and no exponent yields an integer (which promotes to a
 LargeInteger on overflow); a fraction or exponent yields a Float."
JSON class >> parseNumber: scanner
  | negative intPart isFloat fracValue fracScale expPart expNeg c code result |
  negative := false.
  scanner peek = '-' ifTrue: [ scanner next. negative := true ].
  intPart := self readDigits: scanner requireOne: true.
  isFloat := false.
  fracValue := 0.
  fracScale := 1.

  "optional fraction"
  scanner peek = '.' ifTrue: [
    scanner next.
    isFloat := true.
    [ scanner atEnd not and: [ self isDigitCode: scanner peek asInteger ] ]
      whileTrue: [
        c := scanner next.
        fracValue := (fracValue * 10) + (c asInteger - 48).
        fracScale := fracScale * 10 ].
    fracScale = 1
      ifTrue: [ ^ scanner signalError: 'expected digit after ''.''' ] ].

  "optional exponent"
  expPart := 0.
  expNeg := false.
  (scanner atEnd not
    and: [ (scanner peek = 'e') or: [ scanner peek = 'E' ] ]) ifTrue: [
    scanner next.
    isFloat := true.
    (scanner atEnd not and: [ scanner peek = '+' ])
      ifTrue: [ scanner next ].
    (scanner atEnd not and: [ scanner peek = '-' ])
      ifTrue: [ scanner next. expNeg := true ].
    expPart := self readDigits: scanner requireOne: true ].

  isFloat
    ifFalse: [
      result := intPart.
      negative ifTrue: [ result := result negated ].
      ^ result ].

  "Float assembly: integer part + fraction, then scale by the exponent."
  result := intPart asFloat + (fracValue asFloat / fracScale asFloat).
  expPart > 0 ifTrue: [
    | factor i |
    factor := 1.0.
    i := 1.
    [ i <= expPart ] whileTrue: [ factor := factor * 10.0. i := i + 1 ].
    expNeg
      ifTrue: [ result := result / factor ]
      ifFalse: [ result := result * factor ] ].
  negative ifTrue: [ result := result negated ].
  ^ result.

"Read a run of ASCII digits, accumulating an integer. With requireOne true,
 signal an Error when no digit is present."
JSON class >> readDigits: scanner requireOne: requireOne
  | value count c |
  value := 0.
  count := 0.
  [ scanner atEnd not and: [ self isDigitCode: scanner peek asInteger ] ]
    whileTrue: [
      c := scanner next.
      value := (value * 10) + (c asInteger - 48).
      count := count + 1 ].
  (requireOne and: [ count = 0 ])
    ifTrue: [ ^ scanner signalError: 'expected a digit' ].
  ^ value.

"--- stringify: ---

The language exposes no class-test predicate (`isKindOf:`, `class`, ...), so
`stringify:` dispatches by DOUBLE DISPATCH: `JSON stringifyValue:` sends
`jsonStringOn:` to the value, and each class answers its own JSON form. `nil`
and the booleans are immediates that fall through to `Object>>jsonStringOn:`,
so they are special-cased here first."

"Render one protoST value as JSON, dispatching on the value's own class."
JSON class >> stringifyValue: anObject
  anObject isNil ifTrue: [ ^ 'null' ].
  anObject == true ifTrue: [ ^ 'true' ].
  anObject == false ifTrue: [ ^ 'false' ].
  ^ anObject jsonStringOn: self.

"A number as a JSON number literal — its decimal printString."
JSON class >> stringifyNumber: aNumber
  ^ aNumber printString.

"A String as a quoted, escaped JSON string."
JSON class >> stringifyString: aString
  | result i c code |
  result := '"'.
  i := 1.
  [ i <= aString size ] whileTrue: [
    c := aString at: i.
    code := c asInteger.
    code = 34  ifTrue: [ result := result , '\"' ]
    ifFalse: [
    code = 92  ifTrue: [ result := result , '\\' ]
    ifFalse: [
    code = 10  ifTrue: [ result := result , '\n' ]
    ifFalse: [
    code = 9   ifTrue: [ result := result , '\t' ]
    ifFalse: [
    code = 13  ifTrue: [ result := result , '\r' ]
    ifFalse: [
    code = 8   ifTrue: [ result := result , '\b' ]
    ifFalse: [
    code = 12  ifTrue: [ result := result , '\f' ]
    ifFalse: [
    code < 32
      ifTrue: [ result := result , (self unicodeEscape: code) ]
      ifFalse: [ result := result , c ] ] ] ] ] ] ] ].
    i := i + 1 ].
  ^ result , '"'.

"Render a control codepoint as a \\uXXXX escape."
JSON class >> unicodeEscape: code
  | hex digits d rest i |
  digits := '0123456789abcdef'.
  hex := ''.
  rest := code.
  i := 1.
  [ i <= 4 ] whileTrue: [
    d := rest \\ 16.
    hex := (digits at: d + 1) , hex.
    rest := rest // 16.
    i := i + 1 ].
  ^ '\u' , hex.

"An Array / OrderedCollection as a JSON array."
JSON class >> stringifyArray: aCollection
  | parts result first |
  parts := OrderedCollection new.
  aCollection do: [ :each | parts add: (self stringifyValue: each) ].
  result := '['.
  first := true.
  parts do: [ :p |
    first ifFalse: [ result := result , ',' ].
    result := result , p.
    first := false ].
  ^ result , ']'.

"A Dictionary as a JSON object. Non-String keys are coerced via printString."
JSON class >> stringifyDictionary: aDictionary
  | result first keyStr |
  result := '{'.
  first := true.
  aDictionary keysAndValuesDo: [ :k :v |
    first ifFalse: [ result := result , ',' ].
    keyStr := k jsonKeyString.
    result := result , (self stringifyString: keyStr)
                     , ':' , (self stringifyValue: v).
    first := false ].
  ^ result , '}'.

"=== double-dispatch hooks =================================================

`stringifyValue:` sends `jsonStringOn:` to the value; each class answers its
own JSON form below. This is the language-idiomatic substitute for a class
test — protoST exposes no `isKindOf:` / `class`."

"Default: an object that has no JSON representation cannot be encoded."
Object >> jsonStringOn: aJSONClass
  ^ Error signal: 'JSON stringify: cannot encode ' , self printString.

"A String renders as a quoted, escaped JSON string."
String >> jsonStringOn: aJSONClass
  ^ aJSONClass stringifyString: self.

"Every Number (SmallInteger / LargeInteger / Float) renders as a JSON number."
Number >> jsonStringOn: aJSONClass
  ^ aJSONClass stringifyNumber: self.

"A Dictionary renders as a JSON object."
Dictionary >> jsonStringOn: aJSONClass
  ^ aJSONClass stringifyDictionary: self.

"An Array renders as a JSON array."
Array >> jsonStringOn: aJSONClass
  ^ aJSONClass stringifyArray: self.

"An OrderedCollection renders as a JSON array."
OrderedCollection >> jsonStringOn: aJSONClass
  ^ aJSONClass stringifyArray: self.

"Object-key coercion: a String key is used verbatim; any other key falls
 back to its printString so the output is always a valid JSON object."
Object >> jsonKeyString
  ^ self printString.

String >> jsonKeyString
  ^ self.
