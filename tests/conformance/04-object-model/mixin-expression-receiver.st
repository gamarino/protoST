"EXPECT: hi-from-mx"
"§4: T3-b — `subclass:uses:` works with an expression receiver, not just a
bare class identifier. The primary superclass here is reached through a
method call; the mixin method is still inherited by the new class."
Object subclass: #MX.
MX >> hi  ^ 'hi-from-mx'.
Object subclass: #Holder.
Holder >> base  ^ Object.
(Holder new base) subclass: #Made uses: { MX }.
Made new hi.
