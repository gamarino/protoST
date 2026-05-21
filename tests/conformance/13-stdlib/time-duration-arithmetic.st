"EXPECT: 3"
"§13 stdlib: Duration addition composes — 1 s + 2 s is 3 s."
m := Import from: 'time'.
((m Duration seconds: 1) + (m Duration seconds: 2)) asSeconds.
