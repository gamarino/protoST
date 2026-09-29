"Demo 1 - the Smalltalk you already know, run from a plain file:
 classes, blocks, non-local return, exceptions with retry and resume,
 collections, fractions and printOn:."

Error subclass: #InsufficientFunds.

Object subclass: #Account instanceVariableNames: 'owner balance'.

Account class >> owner: aString
  ^ self new setOwner: aString

Account >> initialize
  balance := 0

Account >> setOwner: aString
  owner := aString

Account >> owner
  ^ owner

Account >> balance
  ^ balance

Account >> deposit: amount
  amount <= 0 ifTrue: [^ self error: 'amount must be positive'].
  balance := balance + amount

Account >> withdraw: amount
  amount > balance ifTrue: [^ InsufficientFunds new signal: owner , ' has only ' , balance printString].
  balance := balance - amount

Account >> printOn: aStream
  aStream nextPutAll: owner; nextPutAll: ' $'; print: balance

Object subclass: #Bank instanceVariableNames: 'accounts'.

Bank >> initialize
  accounts := OrderedCollection new

Bank >> open: aName
  ^ accounts add: (Account owner: aName)

Bank >> find: aName
  accounts do: [:each | each owner = aName ifTrue: [^ each]].
  ^ nil

Bank >> richest
  ^ accounts detectMax: [:each | each balance]

Bank >> total
  ^ accounts inject: 0 into: [:sum :each | sum + each balance]

| bank ana attempts |
bank := Bank new.
#('Ana' 'Luis' 'Eva') do: [:name | (bank open: name) deposit: 100].
(bank find: 'Luis') deposit: 50.
Transcript showCr: (bank find: 'Eva') printString.
Transcript showCr: 'richest: ' , bank richest printString.

ana := bank find: 'Ana'.
[ana withdraw: 500]
  on: InsufficientFunds
  do: [:e | Transcript showCr: 'refused: ' , e messageText].

attempts := 0.
[attempts := attempts + 1. ana withdraw: 130]
  on: InsufficientFunds
  do: [:e | ana deposit: 50. e retry].
Transcript showCr: 'after ' , attempts printString , ' attempts: ' , ana printString.

Transcript showCr: ([1 / 0] on: ZeroDivide do: [:e | e resume: 0]) printString.
Transcript showCr: ((1/3) + (1/6)) printString , '  ' , (2 raisedTo: 100) printString.
Transcript showCr: (bank find: 'Nobody') printString.

Transcript showCr: bank total printString.
