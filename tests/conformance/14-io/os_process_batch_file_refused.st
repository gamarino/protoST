"EXPECT: refused untouched"
"OSProcess run: never runs a batch file directly. On Windows protoIO 0.2
 refuses .bat and .cmd targets with an Error, because CreateProcess would run
 them through cmd.exe, which parses the arguments again by rules no quoting
 makes safe (BatBadBut); cmd.exe /c or shell: runs one explicitly. Elsewhere
 the file is not executable, and running it fails too. Either way the batch
 file's command, which would create the marker, does not run."
| dir bat marker r |
dir := FileSystem temp / ('protost-bat-' , Smalltalk pid printString).
dir ensureCreateDirectory.
bat := dir / 'make-marker.bat'.
marker := dir / 'marker'.
bat contents: '@echo off' , (String with: Character cr with: Character lf) ,
  'echo ran > "' , marker fullName , '"' , (String with: Character cr with: Character lf).
r := [OSProcess run: bat fullName arguments: #('x'). 'ran'] on: Error do: [:e | 'refused'].
r := r , ' ' , (marker exists ifTrue: ['touched'] ifFalse: ['untouched']).
dir deleteAll.
r.
