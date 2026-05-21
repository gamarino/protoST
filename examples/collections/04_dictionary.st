"EXPECT: 3"
"-- 04_dictionary.st -------------------------------------------------------"
"-- A Dictionary maps keys to values. This word-frequency count uses        --"
"-- `at:ifAbsent:` to default a missing count to 0, then `at:put:` to       --"
"-- store the incremented count. `at:` reads the final tally.               --"
"-- Expected output: 3  (the count of 'the')                                --"

freq := Dictionary new.
#(the cat sat on the mat the end) do: [ :word |
  | count |
  count := freq at: word ifAbsent: [ 0 ].
  freq at: word put: count + 1 ].

freq at: #the.
