"EXPECT: 0 4 #[1 2 3 4 5 6 7 8] #[1 2 0 4 5 6 7 8] #[7 8] #[] 8 3 #[1 2 0] #[9 9] false"
"BinaryFile: append answers offsets, positioned reads and writes, size, truncation, durability; FileSystem replace:with: and syncDirectory:."
dir := FileSystem temp / ('protost-binfile-basic-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f o1 o2 all patched tail past sz sz2 reread root |
 f := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create #exclusive).
 o1 := f append: #[1 2 3 4].
 o2 := f append: #[5 6 7 8].
 all := f readAt: 0 size: 8.
 f writeAt: 2 bytes: #[0].
 patched := f readAt: 0 size: 8.
 tail := f readAt: 6 size: 100.
 past := f readAt: 20 size: 4.
 sz := f size.
 f truncateTo: 3.
 sz2 := f size.
 f sync.
 f dataSync.
 f close.
 "A second descriptor sees what the first made durable."
 f := BinaryFile open: dir / 'seg' options: #(#read).
 reread := f readAt: 0 size: 10.
 f close.
 "An atomic replacement of an existing file, then the directory entry made durable."
 (dir / 'root') binaryContents: #[1].
 (dir / 'root.tmp') binaryContents: #[9 9].
 FileSystem replace: (dir / 'root.tmp') pathString with: (dir / 'root') pathString.
 FileSystem syncDirectory: dir pathString.
 root := (dir / 'root') binaryContents.
 o1 printString , ' ' , o2 printString , ' ' , all printString , ' ' , patched printString , ' ' ,
   tail printString , ' ' , past printString , ' ' , sz printString , ' ' , sz2 printString , ' ' ,
   reread printString , ' ' , root printString , ' ' , (dir / 'root.tmp') exists printString]
  ensure: [dir deleteAll].
