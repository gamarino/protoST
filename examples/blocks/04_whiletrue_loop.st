"EXPECT: 55"
"-- 04_whiletrue_loop.st ---------------------------------------------------"
"-- `whileTrue:` is a message sent to a block (the condition). This loop    --"
"-- walks the Fibonacci sequence until the value passes 50, leaving the     --"
"-- first Fibonacci number greater than 50.                                 --"
"--                                                                        --"
"-- Top-level variables are module globals — a free name written to with    --"
"-- `:=` is a global; there is no top-level `| ... |` declaration.          --"
"-- Expected output: 55                                                     --"

a := 0.
b := 1.
[ b <= 50 ] whileTrue: [
  | next |
  next := a + b.
  a := b.
  b := next ].
b.
