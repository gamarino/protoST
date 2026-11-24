"EXPECT: #(#(1 2) 'the constant ''left'' of a Both is not set at the end of construction (new)' #(3 4 5) #(2 true true))"
"Spec §6.2 (own or inherited), pre-flight ruling C9: a class that declares
 no variables of its own but uses two mixins, each with one constant, gets
 the generated constructor over both, in the listed order. A class that
 gains constant-carrying behaviours with addBehavior: gets a constructor
 over every constant, inherited first (constantNamesOf's order), and the
 constructors it had before are gone: its metaclass holds that constructor
 and the generated new, nothing else."
Object subclass: #Left constantNames: 'left'.
Left >> left
  ^ left
Object subclass: #Right constantNames: 'right'.
Right >> right
  ^ right
Object subclass: #Both uses: { Left. Right }.
Object subclass: #Grown instanceVariableNames: '' constantNames: 'base'.
Grown >> base
  ^ base
Object subclass: #Tagged constantNames: 'tag'.
Tagged >> tag
  ^ tag
Grown addBehavior: Tagged.
Object subclass: #Third constantNames: 'third'.
Third >> third
  ^ third
Grown addBehavior: Third.

| b g |
b := Both left: 1 right: 2.
g := Grown tag: 4 third: 5 base: 3.
{{b left. b right}. [Both new] on: Error do: [:e | e messageText]. {g base. g tag. g third}.
 {Grown class selectors size. (Grown class selectors includes: #tag:third:base:). (Grown class selectors includes: #new)}}
