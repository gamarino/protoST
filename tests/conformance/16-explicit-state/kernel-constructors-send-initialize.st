"EXPECT: #(#('CSorted' 1 1) #('CSorted' 1 1) #('CWrite' 1 1) #('CWrite' 1 1) #('CWrite' 1 1) #('CRead' 1 1) #('CRandom' 1 1) #('CRandom' 1 1) #('CPoint' 1 1) #('CFraction' 1 1) #('CDuration' 1 1) #('CTime' 1 1) #('CDate' 1 1) #('CDate' 1 1) #('CInterval' 1 1) #('CFile' 1 1) #('CIO' 1 1) #('CExt' 1 1) #('CUUID' 1 1) #('CUUID' 1 1) #('CUUID' 1 1) 'the constant ''k'' of a CForgets is not set at the end of construction (sortBlock:)' 'the constant ''k'' of a CForgets is not set at the end of construction (new)')"
"Spec §6.2 (Task 8 fix round 1): every kernel class-side constructor that
 builds its instance with self basicNew, and so can build a user subclass,
 sends initialize exactly once after the kernel setter and then runs the
 end-of-construction check (M13), as Object class>>new does. Each subclass
 here sets its constant in initialize and counts how often initialize ran:
 every constructor answers #(class 1 1). A subclass whose initialize forgets
 its constant gets M13 naming the constructor that was sent. The kernel
 classes define no initialize, so their setup runs once, before it."
| m |
m := Import from: 'uuid'.
SortedCollection subclass: #CSorted instanceVariableNames: 'inits' constantNames: 'k'.
CSorted >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CSorted >> probe
  ^ {self class name. k. inits}

WriteStream subclass: #CWrite instanceVariableNames: 'inits' constantNames: 'k'.
CWrite >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CWrite >> probe
  ^ {self class name. k. inits}

ReadStream subclass: #CRead instanceVariableNames: 'inits' constantNames: 'k'.
CRead >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CRead >> probe
  ^ {self class name. k. inits}

Random subclass: #CRandom instanceVariableNames: 'inits' constantNames: 'k'.
CRandom >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CRandom >> probe
  ^ {self class name. k. inits}

Point subclass: #CPoint instanceVariableNames: 'inits' constantNames: 'k'.
CPoint >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CPoint >> probe
  ^ {self class name. k. inits}

Fraction subclass: #CFraction instanceVariableNames: 'inits' constantNames: 'k'.
CFraction >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CFraction >> probe
  ^ {self class name. k. inits}

Duration subclass: #CDuration instanceVariableNames: 'inits' constantNames: 'k'.
CDuration >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CDuration >> probe
  ^ {self class name. k. inits}

Time subclass: #CTime instanceVariableNames: 'inits' constantNames: 'k'.
CTime >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CTime >> probe
  ^ {self class name. k. inits}

Date subclass: #CDate instanceVariableNames: 'inits' constantNames: 'k'.
CDate >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CDate >> probe
  ^ {self class name. k. inits}

NumericInterval subclass: #CInterval instanceVariableNames: 'inits' constantNames: 'k'.
CInterval >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CInterval >> probe
  ^ {self class name. k. inits}

FileReference subclass: #CFile instanceVariableNames: 'inits' constantNames: 'k'.
CFile >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CFile >> probe
  ^ {self class name. k. inits}

IOStream subclass: #CIO instanceVariableNames: 'inits' constantNames: 'k'.
CIO >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CIO >> probe
  ^ {self class name. k. inits}

MessagePackExt subclass: #CExt instanceVariableNames: 'inits' constantNames: 'k'.
CExt >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CExt >> probe
  ^ {self class name. k. inits}

UUID yourself subclass: #CUUID
  instanceVariableNames: 'inits'
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
CUUID >> initialize
  k := 1.
  inits := (inits ifNil: [0]) + 1
CUUID >> probe
  ^ {self class name. k. inits}

SortedCollection subclass: #CForgets instanceVariableNames: '' constantNames: 'k'.

| results forgets |
results := OrderedCollection new.
results add: (CSorted sortBlock: [:a :b | a >= b]) probe.
results add: (CSorted new) probe.
results add: (CWrite on: String new) probe.
results add: (CWrite with: 'ab') probe.
results add: (CWrite new) probe.
results add: (CRead on: #(1 2)) probe.
results add: (CRandom seed: 42) probe.
results add: (CRandom new) probe.
results add: (CPoint x: 1 y: 2) probe.
results add: (CFraction numerator: 1 denominator: 2) probe.
results add: (CDuration milliseconds: 5) probe.
results add: (CTime hour: 1 minute: 2 second: 3) probe.
results add: (CDate year: 2026 month: 11 day: 23) probe.
results add: (CDate today) probe.
results add: (CInterval from: 1 to: 5 by: 2) probe.
results add: (CFile path: '/tmp') probe.
results add: (CIO onDescriptor: 1) probe.
results add: (CExt type: 1 data: (ByteArray new: 2)) probe.
results add: (CUUID fromBytes: (ByteArray new: 16)) probe.
results add: (CUUID fromString: '12345678123456781234567812345678') probe.
results add: (CUUID new) probe.
forgets := [:block | block on: Error do: [:e | e messageText]].
results add: (forgets value: [CForgets sortBlock: [:a :b | a < b]]).
results add: (forgets value: [CForgets new]).
results asArray
