"EXPECT: a StepCounter"
"§4: T3-a — an instance of a subclass of an imported class reports the
subclass as its class (printString renders `a StepCounter`, not `a Counter`),
confirming the local subclass — not the imported parent — is its class."
lib := Import from: '_extensible_counter'.
lib Counter subclass: #StepCounter instanceVariableNames: ''.
c := StepCounter newChild.
c initialize.
c printString.
