"EXPECT: 1"
"§9.8: storing under an equal key of another kind replaces the value, not adds an entry (D32)"
d := Dictionary new.
d at: 1 put: 'int'.
d at: 1.0 put: 'float'.
d size.
