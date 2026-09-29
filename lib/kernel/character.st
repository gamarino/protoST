"Kernel: Character protocol. A Character is protoCore's embedded unicode-char
 value; value, asString, asUppercase and asLowercase are primitive."

Character >> isCharacter
  ^ true.

Character >> asCharacter
  ^ self.

Character >> asInteger
  ^ self value.

Character >> asSymbol
  ^ self asString asSymbol.

Character >> displayString
  ^ self asString.

Character >> printString
  ^ '$' , self asString.

Character >> hash
  ^ self value.

Character >> < aCharacter
  ^ self value < aCharacter value.

Character >> > aCharacter
  ^ self value > aCharacter value.

Character >> <= aCharacter
  ^ self value <= aCharacter value.

Character >> >= aCharacter
  ^ self value >= aCharacter value.

Character >> isVowel
  ^ 'aeiouAEIOU' includes: self.

Character >> isLetter
  | v |
  v := self value.
  ^ ((v >= 65) & (v <= 90)) | ((v >= 97) & (v <= 122)) | ((v >= 192) & (v <= 255) & (v ~= 215) & (v ~= 247)).

Character >> isDigit
  ^ (self value >= 48) & (self value <= 57).

Character >> isAlphaNumeric
  ^ self isLetter | self isDigit.

Character >> isUppercase
  ^ self isLetter and: [self asLowercase ~= self].

Character >> isLowercase
  ^ self isLetter and: [self asUppercase ~= self].

Character >> isSeparator
  ^ #(32 9 10 13 12) includes: self value.

Character >> digitValue
  self isDigit ifTrue: [^ self value - 48].
  self isUppercase ifTrue: [^ self value - 55].
  self isLowercase ifTrue: [^ self value - 87].
  ^ -1.

Character class >> value: anInteger
  ^ anInteger asCharacter.

Character class >> space
  ^ 32 asCharacter.

Character class >> tab
  ^ 9 asCharacter.

Character class >> cr
  ^ 13 asCharacter.

Character class >> lf
  ^ 10 asCharacter.
