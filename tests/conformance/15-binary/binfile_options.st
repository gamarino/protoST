"EXPECT: 3 0 #[] 2 #[7 7 1 2] true"
"BinaryFile options: #truncate empties the file, #append starts appends at the current end, #write alone cannot read."
dir := FileSystem temp / ('protost-binfile-options-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| path f before after empty off both writeOnly |
 path := (dir / 'seg') pathString.
 (dir / 'seg') binaryContents: #[7 7 7].
 f := BinaryFile open: path options: #(#read).
 before := f size.
 f close.
 f := BinaryFile open: path options: #(#read #write #truncate).
 after := f size.
 empty := f readAt: 0 size: 4.
 f append: #[7 7].
 f close.
 f := BinaryFile open: path options: #(#read #append).
 off := f append: #[1 2].
 both := f readAt: 0 size: 10.
 f close.
 f := BinaryFile open: path options: #(#write).
 writeOnly := [f readAt: 0 size: 1. false] on: FileSystemError do: [:e | true].
 f close.
 before printString , ' ' , after printString , ' ' , empty printString , ' ' , off printString , ' ' ,
   both printString , ' ' , writeOnly printString]
  ensure: [dir deleteAll].
