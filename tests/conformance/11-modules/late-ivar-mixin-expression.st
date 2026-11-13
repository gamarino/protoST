"EXPECT: 43"
"§4.11 (S37): a class whose uses: clause is an expression -- here an
imported class -- reads the mixin's instance variable."
m := Import from: '_late_base_mod'.
Object subclass: #Kid uses: { m LateBase }.

Kid >> next
  ^ secret + 1.

k := Kid new.
k init.
k next.
