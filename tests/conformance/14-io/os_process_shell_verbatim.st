"EXPECT: verbatim verbatim"
"OSProcess shell: and command: hand the command line to the system shell
 exactly as written: /bin/sh -c, or cmd.exe on Windows, where the quotes
 reach cmd unchanged (echo keeps them) and a quoted & is not a separator.
 Quoting the command as a C runtime argument would hand cmd escaped
 quotes instead, and its echo would print them."
| quoted amp expectQuoted expectAmp |
quoted := OSProcess command: 'echo "a b"'.
amp := OSProcess command: 'echo "x&y"'.
Smalltalk platform = 'windows'
  ifTrue: [expectQuoted := '"a b"'. expectAmp := '"x&y"']
  ifFalse: [expectQuoted := 'a b'. expectAmp := 'x&y'].
(quoted = expectQuoted ifTrue: ['verbatim'] ifFalse: [quoted]) , ' ' ,
  (amp = expectAmp ifTrue: ['verbatim'] ifFalse: [amp]).
