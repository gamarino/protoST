"EXPECT: hi 3 1 false HELLO abc"
"OSProcess: run, shell, exit codes, input, command:."
r := OSProcess run: 'echo' arguments: #('hi').
out := r output trimBoth.
n := (OSProcess shell: 'printf abc | wc -c') output trimBoth.
f := OSProcess run: 'false' arguments: #().
up := (OSProcess run: 'tr' arguments: #('a-z' 'A-Z') input: 'hello') output.
c := OSProcess command: 'printf abc'.
out , ' ' , n , ' ' , f exitCode printString , ' ' , f succeeded printString , ' ' , up , ' ' , c.
