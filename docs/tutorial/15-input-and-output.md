# Chapter 15 — Input and output

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 14](14-for-the-smalltalk-programmer.md)

---

Until 0.5.0 a protoST program could only print. This chapter covers what it
can do now: read and write files, take part in a Unix pipeline, read its
arguments and environment, set its exit status, run other programs, talk TCP
and UDP, and act as an HTTP client or a small HTTP server. The names follow
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
| Kernel (`lib/kernel/io.st`) | always | `Stdio`, `IOStream`, `FileReference`, `File`, `FileSystem`, the program's arguments and environment on `Smalltalk`, `OSProcess`, and the error classes |
| `net` (`lib/net.st`) | `Import from: 'net'` | `Socket` (TCP, optionally TLS), `ServerSocket`, `UDPSocket` |
| `http` (`lib/http.st`) | `Import from: 'http'` | `HTTPClient`, `HTTPResponse`, `HTTPServer`, `HTTPRequest` (it imports `net` and `json` itself) |

Three rules hold everywhere:

- **Text is a UTF-8 `String`; binary data is an `Array` of integers 0–255.**
  protoST has no `ByteArray`.
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
 (dir / 'data.bin') binaryContents: #(0 127 255).
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
#(0 127 255)
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
| Binary data | `binaryContents`, `binaryContents:` (Arrays of 0–255) |
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

## 15.4 Arguments, environment and exit status

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
(OSProcess command: 'echo $PUMP_MODE') printNl.
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
  at <block> (fails.st:3)
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
HTTP only; put a reverse proxy in front of it for TLS (§15.11).

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

## 15.11 Limits

What 0.5.0 does not do:

- **HTTP:** no HTTP/2, no WebSockets, no keep-alive (one request per
  connection), no TLS in `HTTPServer` (TLS is client-side only), no proxy
  support; the client verifies certificates and cannot be told not to.
- **Sockets:** no non-blocking mode and no `select`/`poll` over several
  sockets — use one actor per connection; no Unix-domain sockets.
- **Files:** no `ByteArray` (binary data is an Array of integers); no
  permission changes, file locking or recursive directory walk beyond
  `children` (walk it yourself); no file-system watching.
- **Platforms:** Linux is the platform that is built and tested. The I/O
  layer uses POSIX calls, so native Windows is not supported; on Windows run
  protoST under WSL2 (see [`docs/INSTALLATION.md`](../INSTALLATION.md)).
- **Scale:** each blocked actor holds a thread, and the pool stops at 256
  threads (§15.10). Programs with thousands of simultaneous connections are
  outside what this design serves.

## 15.12 Summary

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
- Not provided: HTTP/2, WebSockets, a TLS server, non-blocking multiplexing,
  native Windows.
- The operating-system layer is [protoIO](https://github.com/gamarino/protoIO),
  shared with protoScala and protoClojure: the same files, processes, sockets
  and HTTP behave the same way in the three languages.

---

[Tutorial index](../TUTORIAL.md)
