"EXPECT-ERROR: instance variable 'id' is constant in Keyed and atomic in Shared"
"Spec §6.1 and M10, conformance 78: two parents that declare one name with
 different modifiers cannot be combined."
Object subclass: #Keyed constantNames: 'id'.
Object subclass: #Shared atomicNames: 'id'.
Object subclass: #Both uses: { Keyed. Shared }.
'not reached' printNl.
