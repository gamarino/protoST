"EXPECT: 7"
"§4: T3-a — an imported module class can be subclassed in the importing
program. The subclass adds a brand-new method; instances of the subclass
still inherit the imported class's other methods (initialize, value)."
lib := Import from: '_extensible_counter'.
lib Counter subclass: #StepCounter instanceVariableNames: ''.

StepCounter >> stepTwice
  self incrementBy: 3.
  self incrementBy: 4.
  ^ self.

c := StepCounter newChild.
c initialize.
c stepTwice.
c value.
