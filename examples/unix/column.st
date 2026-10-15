#!/usr/bin/env protost
"EXPECT-ERROR: usage: column.st N"
"A Unix filter run as a command: it prints the N-th whitespace-separated
 field of every line of standard input, like `awk '{print $N}'`. A line with
 fewer fields prints nothing. Make it executable (chmod +x column.st) and
 chain it with other commands:

   printf 'pump-1 12\npump-2 30\n' | ./column.st 2 | ./sum.st      => 42

 Without N it prints a usage message on standard error and exits with
 status 2, the convention of the Unix tools."
| args n |
args := Smalltalk arguments.
n := args isEmpty ifFalse: [args first asInteger].
(n isNil or: [n < 1]) ifTrue: [
  Stdio stderr nextPutAll: 'usage: column.st N   (N >= 1)'; lf.
  Smalltalk exit: 2].
Stdio stdin linesDo: [:line | | fields |
  fields := line substrings.
  fields size >= n ifTrue: [Stdio stdout nextPutAll: (fields at: n); lf]].
