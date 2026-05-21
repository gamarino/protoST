"XFAIL: EXPECT: true"
"§2.10 + D18: identity comparison == is not bound. Spec-correct: #foo == #foo
should be true (symbols are interned). Currently == is a doesNotUnderstand."
#foo == #foo.
