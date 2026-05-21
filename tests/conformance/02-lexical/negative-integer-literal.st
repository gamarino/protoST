"XFAIL: EXPECT: -1"
"§2.4 + D1: spec says a negative integer literal does not exist; -1 lexes as
binary minus. The SPEC-CORRECT text below says the language should NOT have a
negative literal — but §2.4 itself documents D1, so we pin D1: writing the
literal -1 directly should still yield -1 if it were a real literal.
This is an XFAIL for the documented deviation D1."
-1.
