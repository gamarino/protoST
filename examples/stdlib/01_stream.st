"EXPECT: 30"
"-- 01_stream.st -----------------------------------------------------------"
"-- The `stream` standard-library module. A ReadStream walks a collection   --"
"-- with a cursor (`next`, `peek`, `atEnd`); a WriteStream accumulates      --"
"-- elements (`nextPut:`, `nextPutAll:`) and answers `contents`. Here we    --"
"-- read every element of a source and write the doubled values out.       --"
"-- Expected output: 30  (sum of the doubled values 2 4 6 8 10)             --"

m := Import from: 'stream'.

rs := m ReadStream on: #(1 2 3 4 5).
ws := m WriteStream new.
[ rs atEnd ] whileFalse: [
  ws nextPut: rs next * 2 ].

"contents is #(2 4 6 8 10); sum it back to a single number."
ws contents inject: 0 into: [ :acc :n | acc + n ].
