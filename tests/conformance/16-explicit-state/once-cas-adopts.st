"EXPECT: #(true false 1)"
"Spec §6.3, §6.5, conformance 65: compareAndSet: from: nil on a write-once
 variable is the 'first wins, the others adopt it' form. The second CAS
 answers false, signals nothing and compares no values; oid keeps 1."
Object subclass: #Node instanceVariableNames: '' writeOnceNames: 'oid'.
Node >> oid
  ^ oid

| o |
o := Node new.
{o compareAndSet: #oid from: nil to: 1. o compareAndSet: #oid from: nil to: 2. o oid}
