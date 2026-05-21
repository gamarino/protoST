"EXPECT: 21"
"§8.4: ex resume: v makes signal return v at the signal point"
[ | r | r := Warning signal: 'q'. r + 1 ]
  on: Warning
  do: [ :ex | ex resume: 20 ].
