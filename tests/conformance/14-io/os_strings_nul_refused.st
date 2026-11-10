"EXPECT: 24 refused, nothing created"
"Every string protoST hands to the OS (a path, an argv element, a shell command
 line, an environment name or value, a host name, a module name) is refused with an Error that
 names it when it contains NUL: the OS would read it only up to the NUL."
Import from: 'net'.
| dir nul bad msg refused results created |
dir := FileSystem temp / ('protost-os-nul-' , Smalltalk pid printString).
dir ensureCreateDirectory.
nul := String with: (Character value: 0).
bad := (dir / 'victim') pathString , nul , '.evil'.
msg := [:block | [block value. 'no error'] on: Error do: [:e | e messageText]].
results := {
  msg value: [bad asFileReference delete].
  msg value: [bad asFileReference contents].
  msg value: [bad asFileReference contents: 'x'].
  msg value: [bad asFileReference binaryContents].
  msg value: [bad asFileReference exists].
  msg value: [bad asFileReference writeStream].
  msg value: [bad asFileReference ensureCreateDirectory].
  msg value: [bad asFileReference children].
  msg value: [bad asFileReference fullName].
  msg value: [FileSystem replace: (dir / 'a') pathString with: bad].
  msg value: [FileSystem syncDirectory: bad].
  msg value: [(dir / 'a') pathString asFileReference copyTo: bad].
  msg value: [(dir / 'a') pathString asFileReference moveTo: bad].
  msg value: [Smalltalk getenv: 'PROTOST', nul, 'X'].
  msg value: [Smalltalk setenv: 'PROTOST_NUL_TEST' to: 'a', nul, 'b'].
  msg value: [Smalltalk changeDirectory: bad].
  msg value: [OSProcess run: 'echo' arguments: {'a', nul, 'b'}].
  msg value: [OSProcess shell: 'echo a', nul, 'b'].
  msg value: [OSProcess spawn: 'echo', nul arguments: #()].
  msg value: [Socket connectTo: 'localhost', nul, '.evil' port: 9].
  msg value: [ServerSocket listenOn: 0 host: '127.0.0.1', nul].
  msg value: [UDPSocket bindTo: 0 host: '127.0.0.1', nul].
  msg value: [(UDPSocket bindTo: 0) send: 'x' to: '127.0.0.1', nul port: 9].
  msg value: [Import from: 'net', nul, 'evil']}.
refused := results select: [:each | each includesSubstring: 'must not contain NUL'].
refused size = results size ifFalse: [
  results doWithIndex: [:each :i |
    (each includesSubstring: 'must not contain NUL') ifFalse: [(i printString , ': ' , each) displayNl]]].
created := (dir / 'victim') exists.
dir deleteAll.
(refused size printString , ' refused, ' ,
  (created ifTrue: ['the truncated path was created'] ifFalse: ['nothing created'])) displayNl.
