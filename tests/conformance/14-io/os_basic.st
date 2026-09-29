"EXPECT: true true true yes true"
"Environment, arguments, platform and working directory."
home := (Smalltalk getenv: 'HOME') notNil.
unset := (Smalltalk getenv: 'PROTOST_SURELY_UNSET_VARIABLE') isNil.
env := (Smalltalk environment at: 'PATH' ifAbsent: [nil]) notNil.
Smalltalk setenv: 'PROTOST_IO_TEST' to: 'yes'.
set := Smalltalk getenv: 'PROTOST_IO_TEST'.
cwd := Smalltalk workingDirectory isDirectory.
home printString , ' ' , unset printString , ' ' , env printString , ' ' , set , ' ' , cwd printString.
