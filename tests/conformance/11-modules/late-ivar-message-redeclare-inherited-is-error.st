"EXPECT-ERROR: instance variable 'secret' of Kid is already defined in its superclass LateBase"
"§4.10 (S37): neither may a class built by the subclass: message."
m := Import from: '_late_base_mod'.
(m LateBase) subclass: #Kid instanceVariableNames: 'secret'.
3.
