"EXPECT-ERROR: instance variable 'id' is constant in Keyed and atomic in Shared"
"M10 through addBehavior:: composing a parent that declares a name with a
 different modifier is refused, as it is for uses:."
Object subclass: #Keyed constantNames: 'id'.
Object subclass: #Shared atomicNames: 'id'.
Object subclass: #Both uses: { Keyed }.
Both addBehavior: Shared.
'not reached' printNl.
