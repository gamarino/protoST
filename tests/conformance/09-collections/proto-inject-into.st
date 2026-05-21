"EXPECT: 10"
"§9.2: inject:into: folds over the collection"
#(1 2 3 4) inject: 0 into: [ :acc :each | acc + each ].
