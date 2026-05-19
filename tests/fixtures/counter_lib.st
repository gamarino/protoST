"-- counter_lib.st: a library defining a stateful Counter --"
Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> initialize value := 0.
Counter >> increment value := value + 1.
Counter >> value ^ value.
Counter >> incrementBy: n value := value + n.
