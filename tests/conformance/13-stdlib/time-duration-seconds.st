"EXPECT: 2000"
"§13 stdlib: Duration seconds: n converts to n*1000 milliseconds."
m := Import from: 'time'.
(m Duration seconds: 2) asMilliseconds.
