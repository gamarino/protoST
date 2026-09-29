"EXPECT: 1 2 loaded"
"A method whose last statement has no closing period ends at the blank line:
 the next top-level line, whatever token starts it, is not parsed as a
 continuation of the method's last expression."
Object subclass: #F.
F >> one
  ^ 1

F >> two
  ^ 2

| out |
out := WriteStream on: String new.
out nextPutAll: 'loaded'.
F new one printString , ' ' , F new two printString , ' ' , out contents
