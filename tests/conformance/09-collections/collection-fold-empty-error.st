"EXPECT-ERROR: this collection is empty"
"fold: on an empty collection signals an error, as in Pharo."
#() fold: [:a :b | a + b].
