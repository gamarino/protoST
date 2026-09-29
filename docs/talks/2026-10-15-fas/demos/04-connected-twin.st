"Demo 4 - a connected twin. Readings arrive on standard input from another
 program (04-connected-twin.feed, in Python), the pump is an actor, its state
 is served over HTTP while it runs, and every reading is logged to a file.
 The twin is a plain Unix filter:

   python3 04-connected-twin.feed | protost 04-connected-twin.st"

Import from: 'http'.

Object subclass: #Pump instanceVariableNames: 'state cycles temp'.

Pump >> initialize
  state := 'running'.
  cycles := 0.
  temp := 0

Pump >> reading: aDictionary
  temp := aDictionary at: 'temp'.
  state = 'running' ifTrue: [
    cycles := cycles + 1.
    temp > 75 ifTrue: [state := 'stopped']].
  ^ state

Pump >> statusJson
  ^ '{"state": "' , state , '", "cycles": ' , cycles printString , ', "temp": ' , temp printString , '}'

pump := Pump new asActor.
server := HTTPServer on: 0 handler: [:request |
  (HTTPResponse ok: pump statusJson wait) contentType: 'application/json'].
server startInBackground.

log := FileSystem temp / ('twin-' , Smalltalk pid printString , '.log').
log writeStreamDo: [:out |
  Stdio stdin linesDo: [:line |
    | reading state |
    reading := JSON parse: line.
    state := (pump reading: reading) wait.
    out nextPutAll: line; lf.
    Transcript showCr: 'cycle ' , (reading at: 'cycle') printString ,
      '  temp ' , (reading at: 'temp') printString , '  ' , state]].

state := HTTPClient get: 'http://127.0.0.1:' , server port printString , '/state'.
Transcript showCr: 'GET /state -> ' , state status printString , ' ' , state body.
Transcript showCr: 'log: ' , log lines size printString , ' readings written'.
server stop.
log delete.
