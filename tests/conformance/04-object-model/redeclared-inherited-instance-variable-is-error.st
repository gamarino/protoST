"EXPECT-ERROR: is already defined in its superclass Base"
"A subclass may not redeclare an inherited instance variable."
Object subclass: #Base instanceVariableNames: 'a'.
Base subclass: #Sub instanceVariableNames: 'a'.
Sub new
