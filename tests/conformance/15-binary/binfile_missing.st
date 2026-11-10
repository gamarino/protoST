"EXPECT-ERROR: FileDoesNotExist"
"BinaryFile: without #create a missing file is FileDoesNotExist."
BinaryFile open: (FileSystem temp / ('protost-binfile-missing-' , Smalltalk pid printString)) pathString options: #(#read).
