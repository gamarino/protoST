"EXPECT: #(true nil true false 1)"
"Plan ruling G2: compareAndSet: #w from: nil to: nil on a write-once variable
 writes nothing and answers whether the variable is unset."
Object subclass: #Node instanceVariableNames: '' writeOnceNames: 'oid'.
Node >> oid
  ^ oid

| o |
o := Node new.
{o compareAndSet: #oid from: nil to: nil.
 o oid.
 o compareAndSet: #oid from: nil to: 1.
 o compareAndSet: #oid from: nil to: nil.
 o oid}
