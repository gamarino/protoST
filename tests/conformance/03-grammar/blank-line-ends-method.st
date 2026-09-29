"EXPECT: 1"
"A blank line ends a method body even without a top-level ^ (0.4.0): the
 statements after the last method run as the script, not inside it."
Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> value
  ^ value.

Counter >> reset
  value := 0.

Counter >> increment
  value := value + 1.

c := Counter new.
c reset.
c increment.
c value.
