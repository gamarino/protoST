"EXPECT-ERROR: undeclared variable 'cuont' in Kid"
"§4.5 (S37): assigning a name the class does not declare is an error too."
m := Import from: '_late_base_mod'.
(m LateBase) subclass: #Kid instanceVariableNames: 'count'.

Kid >> f
  cuont := 1.

3.
