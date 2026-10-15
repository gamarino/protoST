#!/usr/bin/env protost
"EXPECT: 0"
"A Unix filter run as a command: it adds the numbers read from standard
 input, one per line, and prints the total. Blank lines are skipped. A line
 that is not a number is reported on standard error with its line number,
 and the program exits with status 1, so `set -e` or `&&` stop the pipeline.

   seq 1 100 | ./sum.st                                          => 5050
   printf 'pump-1 12\npump-2 30\n' | ./column.st 2 | ./sum.st      => 42

 With empty input the total is 0."
| total lineNumber |
total := 0.
lineNumber := 0.
Stdio stdin linesDo: [:line | | text value |
  lineNumber := lineNumber + 1.
  text := line trimBoth.
  text isEmpty ifFalse: [
    value := text asNumber.
    value ifNil: [
      Stdio stderr nextPutAll: 'sum.st: line ', lineNumber printString,
        ' is not a number: ', line; lf.
      Smalltalk exit: 1].
    total := total + value]].
total printNl.
