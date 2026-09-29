"stream -- the stream module.

WriteStream and ReadStream are part of the kernel (lib/kernel/stream.st) and
need no import; this module remains so that programs written against it keep
working. Its classes are the kernel classes: importing it changes nothing.

USAGE
  m := Import from: 'stream'.
  rs := m ReadStream on: #(1 2 3).
  rs next.                => 1
  ws := m WriteStream on: String new.
  ws nextPutAll: 'abc'; nextPut: $d.
  ws contents.            => 'abcd'"

WriteStream subclass: #WriteStream.

ReadStream subclass: #ReadStream.
