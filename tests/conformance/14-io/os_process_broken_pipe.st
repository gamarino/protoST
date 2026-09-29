"EXPECT: 0 after"
"A child that exits without reading its input does not kill the program: the
 write that finds the pipe closed is not allowed to raise SIGPIPE."
| big r |
big := String new: 2000000 withAll: $x.
r := OSProcess run: 'true' arguments: #() input: big.
r exitCode printString , ' after'.
