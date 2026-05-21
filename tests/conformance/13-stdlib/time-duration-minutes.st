"EXPECT: 180"
"§13 stdlib: Duration minutes: n yields n*60 seconds."
m := Import from: 'time'.
(m Duration minutes: 3) asSeconds.
