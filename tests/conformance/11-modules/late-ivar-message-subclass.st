"EXPECT: 45"
"§4.10 (S37): a class built by the subclass: message from an imported class
(the receiver is an expression) reads and writes its own and its inherited
instance variables, also from inside a block."
m := Import from: '_late_base_mod'.
(m LateBase) subclass: #Kid instanceVariableNames: 'own'.

Kid >> bump
  own := 0.
  #(1 2) do: [ :i | secret := secret + i. own := own + i ].
  ^ secret.

k := Kid new.
k init.
k bump.
