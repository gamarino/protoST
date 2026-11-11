# Chapter 15 — Input and output

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 14](14-for-the-smalltalk-programmer.md)

---

Until 0.5.0 a protoST program could only print. This chapter covers what it
can do now: read and write files, take part in a Unix pipeline, read its
arguments and environment, set its exit status, run other programs, talk TCP
and UDP, act as an HTTP client or a small HTTP server, and keep binary data:
bytes, files read and written at explicit offsets, and MessagePack. The names follow
Pharo where Pharo has them (`'data.txt' asFileReference contents`,
`Stdio stdin`, `Smalltalk arguments`), so a Smalltalk programmer can guess
most of them.

The examples that do not need the Internet are run by the documentation
checker against the current build, like those of every other chapter. The few
that reach a real Internet host are marked as not run, and are labelled where
they appear.

## 15.1 Where the pieces live

| Part | Loaded | Provides |
|------|--------|----------|
| Kernel (`lib/kernel/io.st`) | always | `Stdio`, `IOStream`, `FileReference`, `File`, `FileSystem`, the program's arguments and environment on `Smalltalk`, `OSProcess`, and the error classes; since 0.9.0 also `ByteArray`, `BinaryFile`, `MessagePack` and the digests (§15.11) |
| `net` (`lib/net.st`) | `Import from: 'net'` | `Socket` (TCP, optionally TLS), `ServerSocket`, `UDPSocket` |
| `http` (`lib/http.st`) | `Import from: 'http'` | `HTTPClient`, `HTTPResponse`, `HTTPServer`, `HTTPRequest` (it imports `net` and `json` itself) |

Three rules hold everywhere:

- **Text is a UTF-8 `String`; binary data is a `ByteArray`.**
  A `ByteArray` is an immutable value (`#[0 127 255]`); convert text with
  `utf8Encoded` and `utf8Decoded`.
- **Every call that waits blocks the thread that makes it.** A file read, a
  socket read, an `accept` or a child process waits where it is called, as in
  Python or plain C. While it waits it does not hold up the garbage
  collector, and when the waiting thread is an actor's worker the pool adds a
  worker (§15.10).
- **Failures are classed errors** — `FileDoesNotExist`, `ConnectionRefused`
  and the others of §15.9 — that you catch with `on:do:` like any other
  `Error` ([Chapter 7](07-exceptions.md)).

> **In Python** the kernel part is roughly `open`, `pathlib`, `sys.argv`,
> `os.environ` and `subprocess`; `net` is `socket` and `ssl`; `http` is
> `urllib.request` plus `http.server`. **In JavaScript (Node)** it is `fs`,
> `process`, `child_process`, `net`/`dgram`/`tls` and `http`/`https` — but
> Node's calls take callbacks or answer promises, while protoST's block the
> caller and get their concurrency from actors instead.

## 15.2 Files and directories

A `FileReference` names a path; it is not an open file. You get one from a
string, from `File named:`, or from `FileSystem` (`workingDirectory`, `home`,
`temp`, `root`), and you walk down with `/`:

```smalltalk
"-- files-demo.st --"
| dir notes |
dir := FileSystem temp / ('files-demo-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[notes := dir / 'notes.txt'.
 notes contents: 'first line'.
 notes appendContents: (String with: Character lf) , 'second line'.
 notes lines printNl.
 notes size printNl.
 { notes basename. notes basenameWithoutExtension. notes extension } printNl.
 notes copyTo: dir / 'copy.txt'.
 (dir / 'old') createDirectory.
 (dir children collect: [:each | each basename]) printNl.
 (dir files collect: [:each | each basename]) printNl.
 (dir / 'data.bin') binaryContents: #[0 127 255].
 (dir / 'data.bin') binaryContents printNl]
  ensure: [dir deleteAll].
dir exists printNl.
```

```bash
$ ./build/protost files-demo.st
#('first line' 'second line')
22
#('notes.txt' 'notes' 'txt')
#('copy.txt' 'notes.txt' 'old')
#('copy.txt' 'notes.txt')
#[0 127 255]
false
```

The program works in a fresh directory under the system's temporary
directory (named after the process id so that two runs do not collide) and
removes it in an `ensure:` block, so it cleans up even when something inside
fails. The protocol it uses:

| To | Send |
|----|------|
| Read / replace / append text | `contents`, `contents:`, `appendContents:` |
| Read lines | `lines` (an Array), `linesDo: aBlock` |
| Binary data | `binaryContents`, `binaryContents:` (a `ByteArray`) |
| Ask | `exists`, `isFile`, `isDirectory`, `size` (bytes), `modificationTime` (milliseconds since the Unix epoch), `isReadable`, `isWritable` |
| Names | `basename`, `basenameWithoutExtension`, `extension`, `parent`, `fullName` (absolute), `pathString` (as given) |
| List a directory | `children`, `files`, `directories` (sorted by name) |
| Create | `createFile`, `createDirectory`, `ensureCreateDirectory` (with parents) |
| Delete | `delete` (a file or an empty directory; signals if missing), `ensureDelete` (no error if missing), `deleteAll` (a whole tree) |
| Copy and move | `copyTo:`, `moveTo:`, `renameTo: 'newName'` |

`size` counts bytes, `contents size` counts characters: a file holding
`'héllo'` has size 6 and contents of size 5.

### Streams on files

For more than a whole-file read or write, open a stream. The `…StreamDo:`
forms close the stream when the block ends, also when it ends with an error;
`readStream`, `writeStream` and `appendStream` hand you the stream to close
yourself.

```smalltalk
"-- readings.st --"
| dir file total |
dir := FileSystem temp / ('readings-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[file := dir / 'readings.csv'.
 file writeStreamDo: [:out |
   #( #('pump-1' 42) #('pump-2' 17) #('pump-3' 23) ) do: [:row |
     out nextPutAll: row first; nextPut: $,; print: row last; lf]].
 total := 0.
 file linesDo: [:line | total := total + (line substrings: ',') last asNumber].
 total printNl.
 file readStreamDo: [:in | in nextLine printNl. in upToEnd size printNl]]
  ensure: [dir deleteAll].
```

```bash
$ ./build/protost readings.st
82
'pump-1,42'
20
```

A file stream answers the same protocol as the standard streams of the next
section: reading with `nextLine` (nil at the end), `nextLineMax: bytes` (for input you
do not trust: a longer line raises `LineTooLong`), `upToEnd`, `atEnd`,
`linesDo:`, `lines`, `next` / `next: n` (whole UTF-8 characters) and
`nextByteCount: n` (for protocols that count bytes); writing with `nextPutAll:`,
`nextPut:`, `print:` (the `printString`), `display:` and `<<` (the
`displayString`), `lf`/`cr` (both a line feed), `crlf`, `space`, `tab`,
`flush`; and `close`.

> **In Python** `writeStreamDo:` is `with open(path, 'w') as out:` — the block
> plays the part of the `with` body, and `ensure:` guarantees the close.
> `dir / 'notes.txt'` is `pathlib.Path`'s `/`. **In JavaScript (Node)**,
> `contents`/`contents:` are `fs.readFileSync`/`fs.writeFileSync`.

## 15.3 Standard streams and pipelines

`Stdio stdin`, `Stdio stdout` and `Stdio stderr` are the process's standard
streams. `Transcript`, `printNl` and `displayNl` also write to standard
output, and all of them appear in program order.

A program that reads standard input and writes standard output is a Unix
filter. This one prints the lines that contain a pattern; like `grep`, it
reads the files named after the pattern, or standard input when there are
none, prints a usage message on standard error when it has no pattern, and
reports through its exit status whether anything matched:

```smalltalk
"-- grep.st: print the lines that contain PATTERN --"
| args pattern sources found |
args := Smalltalk arguments.
args isEmpty ifTrue: [
  Stdio stderr nextPutAll: 'usage: grep.st PATTERN [FILE...]'; lf.
  Smalltalk exit: 2].
pattern := args first.
sources := args size = 1
  ifTrue: [{ Stdio stdin }]
  ifFalse: [args allButFirst collect: [:name | name asFileReference readStream]].
found := false.
sources do: [:in |
  in linesDo: [:line |
    (line includesSubstring: pattern) ifTrue: [
      found := true.
      Stdio stdout nextPutAll: line; lf]].
  in close].
Smalltalk exit: (found ifTrue: [0] ifFalse: [1]).
```

Run on its own source, it finds its three uses of `Stdio`; run with no
arguments, it prints its usage:

```bash
$ ./build/protost grep.st Stdio grep.st
  Stdio stderr nextPutAll: 'usage: grep.st PATTERN [FILE...]'; lf.
  ifTrue: [{ Stdio stdin }]
      Stdio stdout nextPutAll: line; lf]].
$ ./build/protost grep.st
usage: grep.st PATTERN [FILE...]
```

In a pipeline it reads standard input. The documentation checker runs only
`protost` commands, so this transcript is not run by it:

```console no-run
$ printf 'pump-1 ok\npump-2 ALARM\npump-3 ok\n' | ./build/protost grep.st ALARM
pump-2 ALARM
$ ./build/protost grep.st ALARM grep.st > /dev/null || echo "no alarm"
no alarm
```

Standard error is a separate stream: `2>/dev/null` hides the usage message and
leaves standard output alone. Reading standard input blocks until a line (or
the end of input) arrives; `nextLine` answers nil at the end, which is what
ends `linesDo:`.

### Scripts as commands

A script whose first line is `#!/usr/bin/env protost` is an ordinary command
once it is executable: the system starts `protost` on it, and protoST skips
that line. Two such filters are in `examples/unix/`: `column.st N` prints the
N-th field of each line, and `sum.st` adds the numbers it reads and prints the
total. Each does one thing, so the shell composes them, with each other and
with the standard tools:

```console no-run
$ cd examples/unix && chmod +x column.st sum.st
$ printf 'pump-1 12\npump-2 30\n' | ./column.st 2 | ./sum.st
42
$ seq 1 100 | ./sum.st
5050
$ printf 'p1 4\np2 x\n' | ./column.st 2 | ./sum.st && echo done
sum.st: line 2 is not a number: x
```

The last pipeline shows the exit status at work: `sum.st` reports the bad
line on standard error and exits with status 1, so `&& echo done` does not
run. A filter also stops when the program reading its output exits, so
`./column.st 2 < big.log | head` ends as soon as `head` has its lines.
`/usr/bin/env` looks `protost` up on `PATH`, so a script runs with whichever
`protost` comes first there: an activated venv's, or the system one.


`Smalltalk arguments` answers the words after the script's path on the
command line, as an Array of Strings; `Smalltalk programPath` answers the
script's path.

```smalltalk
"-- args.st --"
Smalltalk arguments printNl.
Smalltalk arguments size printNl.
```

```bash
$ ./build/protost args.st pump-7 'two words' 42
#('pump-7' 'two words' '42')
3
```

The environment is read with `Smalltalk getenv:` (nil when the variable is
not set) or as a whole with `Smalltalk environment` (a Dictionary), and
changed for this process and the programs it starts with
`Smalltalk setenv:to:`:

```smalltalk
"-- env.st --"
(Smalltalk getenv: 'PUMP_ID') printNl.
(Smalltalk getenv: 'NO_SUCH_VARIABLE_HERE') printNl.
(Smalltalk environment includesKey: 'PUMP_ID') printNl.
Smalltalk setenv: 'PUMP_MODE' to: 'test'.
"The system's shell expands the variable: /bin/sh on Linux and macOS,
 cmd.exe on Windows, each with its own syntax."
(OSProcess command: (Smalltalk platform = 'windows'
    ifTrue: ['echo %PUMP_MODE%']
    ifFalse: ['echo $PUMP_MODE'])) printNl.
```

```bash
$ PUMP_ID=7 ./build/protost env.st
'7'
nil
true
'test'
```

**Exit status.** A program that runs to its end exits with status 0. An
unhandled error prints the error and a stack trace on standard error and exits
with status 1:

```smalltalk
"-- fails.st --"
'starting' displayNl.
(Smalltalk getenv: 'PUMP_CONFIG') ifNil: [Error signal: 'PUMP_CONFIG is not set'].
'not reached' displayNl.
```

```bash
$ ./build/protost fails.st
starting
error: PUMP_CONFIG is not set
  at <module> (fails.st:3)
```

`Smalltalk exit: n` ends the program at once with status `n` (0–255), after
flushing pending output; `Smalltalk quit` is `exit: 0`. `grep.st` above uses
both conventions of the Unix tools: 2 for a usage error, 1 for "nothing
found". The remaining system queries are `Smalltalk pid`, `hostName`,
`platform` (`'linux'`), `workingDirectory` and `changeDirectory:`.

> **In Python** these are `sys.argv[1:]`, `os.environ.get`, `sys.exit(n)`;
> **in Node**, `process.argv.slice(2)`, `process.env`, `process.exit(n)`.

## 15.5 Running other programs

`OSProcess` runs a program to completion and answers an `OSProcessResult`
with its `exitCode`, its standard `output` and its `errorOutput`; `succeeded`
is `exitCode = 0`. `run:arguments:` passes the arguments to the program
directly (no shell, so no quoting problems); `shell:` hands a command line to
`/bin/sh`; `input:` variants feed the program's standard input. `command:` is
the short form for "the output of this command line", without its final
newline:

```smalltalk
OSProcess command: 'echo hello'                                        "=> 'hello'"
(OSProcess run: 'tr' arguments: #('a-z' 'A-Z') input: 'pump') output   "=> 'PUMP'"
(OSProcess shell: 'printf "a\nb\n" | wc -l') output trimBoth           "=> '2'"
(OSProcess shell: 'exit 3') exitCode                                   "=> 3"
(OSProcess shell: 'exit 3') succeeded                                  "=> false"
```

`command:` signals `OSProcessError` when the command fails, and `run:…`
signals it when the program cannot be started at all:

```smalltalk
[OSProcess command: 'exit 4'] on: OSProcessError do: [:e | e messageText]     "=> 'exit 4 exited with 4: '"
[OSProcess run: 'no-such-program' arguments: #()] on: OSProcessError do: [:e | e class name]   "=> 'OSProcessError'"
```

A program that should run alongside yours is started with
`spawn:arguments:`, which answers its process id at once; it shares your
standard streams. `waitFor:` waits for it and answers its exit status (128 +
the signal number when a signal ended it), and `kill:` sends it `SIGTERM`
(`kill:signal:` any signal):

```smalltalk
pid := OSProcess spawn: 'sleep' arguments: #('30').
OSProcess kill: pid.
OSProcess waitFor: pid        "=> 143"
```

`run:…` collects the whole output in memory before it answers; for a program
that produces output without end, use `spawn:` and let it write to your
standard output.

## 15.6 Sockets

`Import from: 'net'` gives TCP and UDP. A `Socket` is a stream with the
protocol of §15.2 plus `close`, `peerName` and `timeout:`; a `ServerSocket`
listens and `accept`s connections.

The natural shape of a server in protoST is **one actor per connection**:
an acceptor takes connections and hands each to a new actor, which serves it
for as long as the client stays. This echo server answers every line in
capitals; three clients, each an actor too, talk to it at the same time:

```smalltalk
"-- echo.st: a TCP server with one actor per connection --"
Import from: 'net'.
Object subclass: #Session.
Session >> serve: aSocket
  | count |
  count := 0.
  [aSocket linesDo: [:line |
     count := count + 1.
     aSocket nextPutAll: line asUppercase; lf; flush]]
    ensure: [aSocket close].
  ^ count

Object subclass: #Acceptor.
Acceptor >> accept: n on: aServer
  ^ (1 to: n) collect: [:i | Session new asActor serve: aServer accept]

Object subclass: #Client.
Client >> talk: aPort words: anArray
  | s replies |
  s := Socket connectTo: '127.0.0.1' port: aPort.
  replies := anArray collect: [:w | s nextPutAll: w; lf; flush. s nextLine].
  s close.
  ^ replies

server := ServerSocket listenOn: 0 host: '127.0.0.1'.
sessions := Acceptor new asActor accept: 3 on: server.
clients := #( #('ab' 'cd') #('ef') #('gh' 'ij' 'kl') ) collect: [:words |
  Client new asActor talk: server port words: words].
(clients collect: [:f | f wait]) printNl.
((sessions wait collect: [:f | f wait]) inject: 0 into: [:a :b | a + b]) printNl.
server close.
```

```bash
$ ./build/protost echo.st
#(#('AB' 'CD') #('EF') #('GH' 'IJ' 'KL'))
6
```

Port 0 asks the system for any free port; `server port` answers the one it
got. `listenOn: 8080` alone listens on every interface; `listenOn:host:` with
`'127.0.0.1'` only on this machine. Each write to a socket is sent when it is
made; the examples send `flush` anyway, which costs nothing and keeps the code
correct if it is later pointed at standard output, where output is buffered.
`acceptTimeout: ms` answers nil when no client arrives in time, and
`timeout: ms` on a socket makes a read or write that waits longer signal
`ConnectionTimedOut`.

`Socket connectTo:port:` gives up after 10 seconds
(`connectTo:port:timeout:` sets another limit). `tlsHost: 'name'` upgrades a
connected socket to TLS, verifying the server's certificate and that it names
that host; `Socket connectToTLS: 'host' port: 443` does both steps.
`tlsHostUnverified:` skips the verification, for a test server with a
self-signed certificate only.

**UDP.** A `UDPSocket` is bound to a port and sends and receives datagrams;
a received datagram is `{data. host. port}`, and `receiveTimeout:` answers nil
when nothing arrives in time:

```smalltalk
"-- udp.st --"
Import from: 'net'.
rx := UDPSocket bindTo: 0 host: '127.0.0.1'.
tx := UDPSocket bindTo: 0 host: '127.0.0.1'.
tx send: 'temp=21.5' to: '127.0.0.1' port: rx port.
datagram := rx receiveTimeout: 2000.
datagram first printNl.
((datagram at: 3) = tx port) printNl.
(rx receiveTimeout: 100) printNl.
rx close. tx close.
```

```bash
$ ./build/protost udp.st
'temp=21.5'
true
nil
```

## 15.7 The HTTP client

`Import from: 'http'` gives an HTTP/1.1 client for `http` and `https` URLs.
`HTTPClient get:`, `delete:`, `post:body:`, `put:body:`, `post:json:` and
`put:json:` (which send an object as JSON), and the general
`request:url:headers:body:` answer an `HTTPResponse`: `status`, `reason`,
`headers` (a Dictionary with lower-case names), `headerAt:`, `body` (a
String), `json` (the body parsed with `JSON parse:`) and `isSuccess` (a 2xx
status). Chunked bodies are decoded and redirects followed, up to five. A
relative `Location` resolves against the request URL; a redirect to another
host or port does not carry the headers you set (an `Authorization` stays with
the host you gave it to), and a redirect from `https` to `http` is refused
with a `NetworkError`. A method, URL or header that contains a line break is
refused with an `Error` before anything is sent.
`HTTPClient timeout: ms` sets the connect and read limit for every later
request (default 30 seconds).

Against a real server on the Internet — **not run by the documentation
checker**, because it needs network access and the answer can change:

```smalltalk no-run
Import from: 'http'.
r := HTTPClient get: 'https://api.github.com/repos/gamarino/protoST'.
r status printNl.                "prints 200"
(r json at: 'name') printNl.     "prints 'protoST'"
```

An `https` request verifies the certificate chain against the system's
certificate store and checks the host name; a failure signals a
`NetworkError` whose text says why (`'TLS certificate: hostname mismatch
(…)'`). `HTTPClient` has no switch to turn verification off.

The rest of this section talks to a server on the same machine, so it runs
anywhere. The server is the subject of the next section; here it only
answers the request in JSON:

```smalltalk
"-- client.st --"
Import from: 'http'.
server := HTTPServer on: 0 handler: [:req |
  Dictionary new
    at: 'method' put: req method;
    at: 'path' put: req path;
    at: 'name' put: (req query at: 'name' ifAbsent: ['?']);
    at: 'body' put: req body;
    yourself].
server startInBackground.
base := 'http://127.0.0.1:' , server port printString.

r := HTTPClient get: base , '/greet?name=Ana'.
r printNl.
(r headerAt: 'Content-Type') printNl.
(r json at: 'name') printNl.

r := HTTPClient post: base , '/readings'
       json: (Dictionary new at: 'pump' put: 'pump-1'; at: 'flow' put: 42; yourself).
((JSON parse: (r json at: 'body')) at: 'flow') printNl.

r := HTTPClient request: 'PUT' url: base , '/mode'
       headers: (Dictionary new at: 'X-Token' put: 'secret'; yourself)
       body: 'manual'.
(r json at: 'method') printNl.
server stop.
```

```bash
$ ./build/protost client.st
an HTTPResponse (200 OK)
'application/json'
'Ana'
42
'PUT'
```

`JSON` is available without an import of its own because `http` imports the
`json` module ([Chapter 9](09-standard-library.md), §9.6). The client sends
one request per connection (`Connection: close`).

## 15.8 A small HTTP server

`HTTPServer on: port handler: aBlock` builds a server; the block receives an
`HTTPRequest` — `method`, `path`, `query` (a Dictionary of the decoded query
parameters), `headers`, `headerAt:`, `body`, `json`, `peer` — and answers the
response:

| The handler answers | The client receives |
|---------------------|---------------------|
| an `HTTPResponse` (`ok:`, `json:`, `status:body:`, `notFound`, `noContent`, then `contentType:` or `headerAt:put:` to adjust it) | that response |
| a String | 200, `text/plain` |
| nil | 204, no body |
| any other object | 200, the object as JSON |

An error the handler does not catch answers a plain 500 (`Internal Server
Error`: the error's text could reveal internals, so it is not sent) and is
reported on standard error; the server goes on serving. `startInBackground` serves on an actor and answers at
once; `start` serves on the calling thread until something sends `stop`.
Each connection is handled by its own actor, so a slow request does not hold
up the others.

A handler usually talks to the rest of the program through actors. Here the
state lives in a `Counter` actor, and a `Visitor` actor plays the client
while the main program serves with `start`; the `/quit` route stops the
server:

```smalltalk
"-- counter-server.st --"
Import from: 'http'.
Object subclass: #Counter instanceVariableNames: 'n'.
Counter >> initialize
  n := 0
Counter >> add: k
  n := n + k.
  ^ n
Counter >> value
  ^ n

Object subclass: #Visitor.
Visitor >> visit: base
  ^ { (HTTPClient post: base , '/add' json: (Dictionary new at: 'by' put: 5; yourself)) body.
      (HTTPClient get: base , '/count') body.
      (HTTPClient get: base , '/boom') status.
      (HTTPClient get: base , '/nowhere') status.
      (HTTPClient get: base , '/quit') body }

counter := Counter new asActor.
server := HTTPServer on: 0 handler: [:req |
  (req method = 'POST' and: [req path = '/add'])
    ifTrue: [Dictionary new at: 'count' put: (counter add: (req json at: 'by')) wait; yourself]
    ifFalse: [req path = '/count'
      ifTrue: [Dictionary new at: 'count' put: counter value wait; yourself]
      ifFalse: [req path = '/boom'
        ifTrue: [Error signal: 'sensor offline']
        ifFalse: [req path = '/quit'
          ifTrue: [server stop. 'bye']
          ifFalse: [HTTPResponse notFound]]]]].
visit := Visitor new asActor visit: 'http://127.0.0.1:' , server port printString.
server start.
visit wait do: [:each | each printNl].
```

```bash
$ ./build/protost counter-server.st
HTTPServer: error in handler: sensor offline
'{"count":5}'
'{"count":5}'
500
404
'bye'
```

A server meant to keep running is the same code with a fixed port and no
`/quit` route. This one is **not run** by the checker, because it never ends:

```smalltalk no-run
Import from: 'http'.
server := HTTPServer on: 8080 handler: [:req |
  req path = '/hello'
    ifTrue: ['hello, ' , (req query at: 'name' ifAbsent: ['world'])]
    ifFalse: [HTTPResponse notFound]].
server start.
```

```console no-run
$ curl 'http://localhost:8080/hello?name=Ana'
hello, Ana
```

The server refuses what it should not have to read before the handler runs: a
request or header line over 8 KiB (414 or 431), more than 100 header lines
(431), a malformed `Content-Length` (400) and a body over `maxBodySize:` (413;
the default is 64 MiB, `server maxBodySize: 1024 * 1024` lowers it).

`HTTPServer on: 8080` listens on every interface. The server speaks plain
HTTP only; put a reverse proxy in front of it for TLS (§15.12).

## 15.9 When things go wrong

Every failure is an `Error` of a class that says what happened:

```
Error
├── FileSystemError        a file operation failed (permission, not empty, is a directory, …)
│   ├── FileDoesNotExist   the file or directory is not there
│   └── FileAlreadyExists  createDirectory on an existing path
├── NetworkError           a socket, TLS or HTTP failure
│   ├── ConnectionRefused  nothing listens on that port
│   ├── ConnectionTimedOut a connect, read or write took longer than its timeout
│   ├── NameLookupFailure  the host name does not resolve
│   └── BodyTooLarge       an HTTP body over the server's maxBodySize:
├── LineTooLong            nextLineMax: met a longer line
└── OSProcessError         a program could not be started, or command: failed
```

Catch the specific class when you have something to do about that case, and
a parent class to cover a family:

```smalltalk
"-- errors.st --"
Import from: 'net'.
| dir outcome listener port |
dir := FileSystem temp / ('errors-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[outcome := [(dir / 'config.txt') contents]
   on: FileDoesNotExist do: [:e | 'default config'].
 outcome printNl.
 (dir / 'sub') createDirectory.
 (dir / 'sub' / 'x') contents: 'x'.
 outcome := [(dir / 'sub') delete. 'deleted']
   on: FileSystemError do: [:e | e class name].
 outcome printNl]
  ensure: [dir deleteAll].

listener := ServerSocket listenOn: 0.
port := listener port.
listener close.
outcome := [Socket connectTo: '127.0.0.1' port: port]
  on: ConnectionRefused do: [:e | 'nobody listening'].
outcome printNl.
outcome := [Socket connectTo: 'no-such-host.invalid' port: 80]
  on: NetworkError do: [:e | e class name].
outcome printNl.
```

```bash
$ ./build/protost errors.st
'default config'
'FileSystemError'
'nobody listening'
'NameLookupFailure'
```

`messageText` carries the operation, the path or address, and the system's
reason, for example `'cannot read /tmp/x/config.txt: No such file or
directory'`. Pair every stream or socket you open yourself with `ensure:`
(or use the `…StreamDo:` forms), so it is closed on the error path too.

## 15.10 Blocking I/O and actors

protoST's I/O calls block the thread that makes them. For the main program
that is simply sequential code. For actors it has three consequences worth
knowing.

**An actor blocked in I/O handles no other message** until the call
returns, exactly as an actor that `wait`s on a future
([Chapter 10](10-actors-and-futures.md)). So do not ask one actor to serve
several connections: give each connection its own actor, as `echo.st` and
`HTTPServer` do. There is no `select` over many sockets from one actor.

**The worker pool grows while workers block.** Actors run on a pool of worker
threads (as many as the machine has cores, at most eight; `PROTOST_WORKERS`
overrides it). When a worker enters a blocking call and fewer than that many
workers would remain free, the runtime starts another worker, so actors that
are ready to run are not starved by actors that wait. The pool stops growing
at 256 threads, and threads added this way stay until the program ends. In
this program forty sessions are blocked in `nextLine` at the same time, on a
machine with fewer cores than that, and every one of them is answered:

```smalltalk
"-- many.st --"
Import from: 'net'.
Object subclass: #Session.
Session >> serve: aSocket
  | line |
  line := aSocket nextLine.
  aSocket nextPutAll: line; lf; flush; close.
  ^ line size

Object subclass: #Acceptor.
Acceptor >> accept: n on: aServer
  ^ (1 to: n) collect: [:i | Session new asActor serve: aServer accept]

n := 40.
server := ServerSocket listenOn: 0 host: '127.0.0.1'.
sessions := Acceptor new asActor accept: n on: server.
sockets := (1 to: n) collect: [:i | Socket connectTo: '127.0.0.1' port: server port].
Object sleep: 200.
sockets do: [:s | s nextPutAll: 'x'; lf; flush].
replies := sockets collect: [:s | | r | r := s nextLine. s close. r].
(replies count: [:r | r = 'x']) printNl.
((sessions wait collect: [:f | f wait]) inject: 0 into: [:a :b | a + b]) printNl.
server close.
```

```bash
$ ./build/protost many.st
40
40
```

**A program ends when its actors do.** After the last top-level statement,
the runtime waits for the messages its actors are running. An actor still
blocked in `accept` or a read, or an `HTTPServer` started with
`startInBackground` and never sent `stop`, keeps the program running (Ctrl-C
still ends it). Stop servers and close sockets before the end, as the examples
above do, or end the program explicitly with `Smalltalk exit: 0`.

**A blocked call does not stop the garbage collector.** While it waits, the
thread leaves the collector's quorum, so a collection triggered by other
actors proceeds without it.

The design this replaces in the long run — a reactor that turns a blocking
call into a yield of the actor, so that no thread waits at all — is Track 15
of [`docs/ROADMAP.md`](../ROADMAP.md). It is not implemented.

## 15.11 Bytes, binary files and MessagePack

Since 0.9.0 binary data has a type of its own. A `ByteArray` is an immutable
sequence of integers from 0 to 255; `#[0 127 255]` is its literal. It is a
value, like a `String`: `at:put:` signals `ModificationForbidden`, and every
operation that would change one answers a new `ByteArray`. A `ByteArray` is
not text: it has no `asString`, and the way between the two is explicit,
through UTF-8.

```smalltalk
"-- bytes-demo.st --"
| bytes out in |
bytes := 'héllo' utf8Encoded.
bytes printNl.
bytes utf8Decoded printNl.
(bytes copyFrom: 1 to: 2) printNl.
(bytes , #[0 255]) hex printNl.
(ByteArray fromHex: 'cafe') printNl.
out := WriteStream on: ByteArray new.
out nextPutUnsigned: 258 width: 2 bigEndian: true.
out nextPutSigned: -2 width: 4 bigEndian: false.
out nextPutFloat64: 1.5 bigEndian: true.
in := out contents.
in printNl.
(in unsignedShortAt: 1 bigEndian: true) printNl.
(in signedLongAt: 3 bigEndian: false) printNl.
(in float64At: 7 bigEndian: true) printNl.
```

```bash
$ ./build/protost bytes-demo.st
#[104 195 169 108 108 111]
'héllo'
#[104 195]
'68c3a96c6c6f00ff'
#[202 254]
#[1 2 254 255 255 255 63 248 0 0 0 0 0 0]
258
-2
1.5
```

`utf8Decoded` is strict: a byte sequence that is not valid UTF-8 signals an
`Error` naming the byte and its position, and never produces a replacement
character. Indices are 1-based and checked, and a number that does not fit its
width is an error, never a wrapped value: `out nextPutUnsigned: 256 width: 1
bigEndian: true` signals. Floats are written and read through their bit
patterns, so `-0.0`, infinities and NaN survive a round trip.

Random bytes for an identifier, a nonce or a key come from the operating
system's cryptographically secure generator: `ByteArray randomBytes: 16`
answers 16 of them, a new value on every call and, like every `ByteArray`,
immutable. The size is an Integer from 0 to 1048576; anything else signals
`InvalidArgument`. (`Random` is a seeded, reproducible generator for
simulations and tests, never for secrets.)

```smalltalk
(ByteArray randomBytes: 16) size        "=> 16"
(ByteArray randomBytes: 16) = (ByteArray randomBytes: 16)   "=> false"
```

A byte stream takes bytes as a `ByteArray`: `nextPutAll:` (and `<<`) refuse an
`Array` of Integers, which goes in through `ByteArray withAll: #(1 2 3)`, and
a `String`, which goes in through `utf8Encoded`. The same rule holds for the
I/O methods that write bytes (`binaryContents:`, `nextPutBytes:`, UDP
`send:`): since 0.10.0 they refuse an `Array` with an `Error` that names
`ByteArray withAll:` (0.9.0 accepted one with a deprecation warning).

The same byte arrays feed the checksums and digests, which are one-shot
functions (there are no incremental objects):

| To | Send |
|----|------|
| CRC-32 (an Integer) | `crc32`, `crc32Continuing: aCrc`, `CRC32 combine: a with: b length: n` |
| BLAKE2b (a `ByteArray`) | `blake2b: size`, `blake2b: size key: aKey` |
| SHA-256, HMAC-SHA-256 (a `ByteArray`) | `sha256`, `HMAC sha256Key: aKey message: aByteArray` |
| Base64 | `base64Encoded` (a `String`), `aString base64Decoded` (a `ByteArray`) |

```smalltalk
"-- digest-demo.st --"
| data |
data := 'hello' utf8Encoded.
data crc32 printNl.
data sha256 hex printNl.
(data blake2b: 16) hex printNl.
data base64Encoded printNl.
'aGVsbG8=' base64Decoded utf8Decoded printNl.
```

```bash
$ ./build/protost digest-demo.st
907060870
'2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824'
'46fb7408d4f285228f4af516ea25851b'
'aGVsbG8='
'hello'
```

Base64 decoding is strict: it accepts only the standard alphabet with correct
padding and canonical trailing bits, so `'abd='` is refused where some
libraries accept it.

### Files with offsets: `BinaryFile`

`FileReference` reads and writes whole files. A program that keeps data in a
file (a log, a store) needs to name the offset of every read and write, to
append, to force the data to disk and to lock the file against other
processes. `BinaryFile` does exactly that and nothing else: it has no current
position and no stream protocol.

```smalltalk
"-- binfile-demo.st --"
| dir file second |
dir := FileSystem temp / ('binfile-demo-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[file := BinaryFile open: dir / 'log.bin' options: #(#read #write #create).
 (file append: #[1 2 3 4]) printNl.
 (file append: #[5 6]) printNl.
 (file readAt: 2 size: 3) printNl.
 file writeAt: 0 bytes: #[9].
 (file readAt: 0 size: 100) printNl.
 file size printNl.
 file truncateTo: 2.
 file size printNl.
 file sync.
 file tryLock printNl.
 second := BinaryFile open: dir / 'log.bin' options: #(#read).
 second tryLock printNl.
 file unlock.
 second tryLock printNl.
 second close.
 file close.
 file isClosed printNl]
  ensure: [dir deleteAll].
```

```bash
$ ./build/protost binfile-demo.st
0
4
#[3 4 5]
#[9 2 3 4 5 6]
6
2
true
false
true
true
```

Offsets start at 0, as in the operating system. `append:` answers the offset
where the bytes start. `readAt:size:` answers fewer bytes only at the end of
the file, and an empty `ByteArray` past it. `sync` flushes data and metadata,
`dataSync` only what is needed to read the data back. The options are a list
of symbols (`#read #write #create #exclusive #truncate #append`); an unknown
one signals an `Error` before anything is opened.

`tryLock` and `lock` take an exclusive advisory lock on the file (`flock` on
Unix). Two descriptors on the same file conflict, also within one process, as
the example shows. The lock is released by `unlock`, by `close` or when the
process exits. `FileSystem replace: from with: to` renames a file over
another atomically, and `FileSystem syncDirectory: aPath` makes such a rename
durable. The usual way to replace a file is: write a temporary file next to it,
`sync` it, `replace:with:` and `syncDirectory:`.

A `BinaryFile` is not closed when it is garbage: it stays open, with its lock,
until you `close` it or the program ends. Using it after `close` signals an
`Error`; closing twice does nothing. Keep to this rule: **one appender at a
time per file**. `append:` answers the end of the file just before it writes,
so two concurrent appenders could receive the same offset. protoST refuses the
second concurrent `append:` on the same `BinaryFile` with an `Error` instead
of letting the records overlap. The same guard refuses a `truncateTo:` while
anything writes, and a `writeAt:bytes:` that extends the file while an
`append:` runs, so a write never lands past the end of a file that was cut
meanwhile. It cannot detect appenders in other processes, or on a second
`BinaryFile` over the same path: use `lock` for them.

### MessagePack

`MessagePack` converts between a tree of values and the
[MessagePack](https://msgpack.org) format, which other languages read as well.

| protoST | MessagePack |
|---------|-------------|
| `nil`, `true`, `false` | nil, bool |
| an Integer from -2^63 to 2^64-1 | the smallest int or uint (outside the range: `Error`) |
| a `Float` | float 64 |
| a `String` (a `Symbol` too) | str |
| a `ByteArray` | bin |
| an `Array`, an `OrderedCollection` | array |
| a `Dictionary` | map |
| `MessagePackMap` | map, in the order it holds |
| `MessagePackExt` | ext (`type:data:`) |

Anything else (a `Character`, a `Set`, a `Fraction`, an object of your own
class) signals an `Error`, as does a structure that contains itself. A
`Dictionary` has no defined order, so the bytes of a document with several keys
can differ from run to run; use a `MessagePackMap` (`withAll:` an Array of
`Associations`) when the bytes must be exact, and `decodeOrdered:` to read a
document into one.

```smalltalk
"-- msgpack-demo.st --"
| doc bytes map |
doc := Array with: 1 with: 'a' with: nil with: 2.5 with: #[7].
bytes := MessagePack encode: doc.
bytes printNl.
(MessagePack decode: bytes) printNl.
map := MessagePackMap withAll: (Array with: 'x' -> 1 with: 'y' -> 2).
(MessagePack encode: map) printNl.
(MessagePack decodeOrdered: (MessagePack encode: map)) printNl.
(MessagePack decode: (MessagePack encode: (Dictionary new at: 'k' put: 3; yourself))) printNl.
[MessagePack decode: #[1 2]] on: Error do: [:e | e messageText printNl].
```

```bash
$ ./build/protost msgpack-demo.st
#[149 1 161 97 192 203 64 4 0 0 0 0 0 0 196 1 7]
#(1 'a' nil 2.5 #[7])
#[130 161 120 1 161 121 2]
a MessagePackMap#('x'->1 'y'->2)
a Dictionary('k'->3)
'MessagePack decode: 1 trailing byte(s) after the first object (use decodeFirst: to allow them)'
```

Decoding is strict about what it reads: a truncated document, a trailing
byte, invalid UTF-8 in a `str`, nesting deeper than 512 levels and a map key
that is not a `str` or a `bin` are errors (`decode:strictKeys: false`
relaxes the key rule). `decodeFirst:` answers an Association from the first
object to the number of bytes it used, for a stream of documents. The ext type
-1 (MessagePack's timestamp) is decoded as a plain `MessagePackExt`.

## 15.12 Limits

What 0.9.0 does not do:

- **HTTP:** no HTTP/2, no WebSockets, no keep-alive (one request per
  connection), no TLS in `HTTPServer` (TLS is client-side only), no proxy
  support; the client verifies certificates and cannot be told not to.
- **Sockets:** no non-blocking mode and no `select`/`poll` over several
  sockets — use one actor per connection; no Unix-domain sockets.
- **Files:** a `ByteArray` cannot be changed in place (build a new one, or use a
  byte `WriteStream`); locking is for `BinaryFile` only (§15.11), and it is
  advisory; no permission changes or recursive directory walk beyond
  `children` (walk it yourself); no file-system watching. A `BinaryFile`
  has no position and no stream protocol, and it cannot tell that another
  process appends to the same file.
- **MessagePack:** one-shot `encode:` and `decode:`, no incremental reader;
  no timestamp type (ext -1 is a plain `MessagePackExt`); the bytes of a
  `Dictionary` with several keys depend on its internal order.
- **Platforms:** Linux, macOS and Windows (MSVC) are built and tested in
  CI; the released package is a Linux `.deb`. On Windows a few things
  differ (`cmd.exe` as the shell, batch files refused by `run:`, `/` in the
  paths answered); see [`docs/INSTALLATION.md`](../INSTALLATION.md),
  "Windows (MSVC)", or run protoST under WSL2.
- **Scale:** each blocked actor holds a thread, and the pool stops at 256
  threads (§15.10). Programs with thousands of simultaneous connections are
  outside what this design serves.

## 15.13 Summary

- The kernel reads and writes files (`FileReference`: `contents`, `lines`,
  `writeStreamDo:`, `children`, `copyTo:`, `deleteAll`, …), the standard
  streams (`Stdio`), the command line and environment (`Smalltalk arguments`,
  `getenv:`, `exit:`) and runs programs (`OSProcess`); no import needed.
- `Import from: 'net'` adds TCP (`Socket`, `ServerSocket`, TLS client) and
  UDP (`UDPSocket`); `Import from: 'http'` adds `HTTPClient` (http, https,
  JSON) and `HTTPServer`.
- Failures are classed `Error`s: `FileDoesNotExist`, `FileAlreadyExists`,
  `FileSystemError`, `ConnectionRefused`, `ConnectionTimedOut`,
  `NameLookupFailure`, `NetworkError`, `OSProcessError`.
- Calls block; concurrency comes from actors — one per connection — and the
  worker pool grows while workers are blocked, up to 256 threads.
- `ByteArray` is binary data (`#[1 2 255]`, immutable, with `utf8Encoded` and
  `utf8Decoded` for text, byte-order readers and writers, CRC-32, BLAKE2b,
  SHA-256, HMAC and base64); `BinaryFile` is a file read and written at
  explicit offsets, with `sync` and a lock; `MessagePack` reads and writes the
  MessagePack format (§15.11).
- Not provided: HTTP/2, WebSockets, a TLS server, non-blocking multiplexing,
  native Windows.
- The operating-system layer is [protoIO](https://github.com/gamarino/protoIO),
  shared with protoScala and protoClojure: the same files, processes, sockets
  and HTTP behave the same way in the three languages.

---

[Tutorial index](../TUTORIAL.md)
