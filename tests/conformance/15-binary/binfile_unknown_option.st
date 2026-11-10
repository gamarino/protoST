"EXPECT-ERROR: BinaryFile: unknown option #rw"
"BinaryFile: an unknown option symbol signals Error before anything is opened."
BinaryFile open: (FileSystem temp / 'protost-binfile-never-created') pathString options: #(#read #rw).
