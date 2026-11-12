"EXPECT: #('secret' 'own')"
"§4.10 (S37): the subclass: message records the instance-variable names it
is given, as the textual declaration does, so reflection sees them."
m := Import from: '_late_base_mod'.
(m LateBase) subclass: #Kid instanceVariableNames: 'own'.
Kid allInstVarNames printString.
