"EXPECT: true 11 true false"
"BinaryFile: a second close is a no-op; every other use after close signals Error naming the closed file."
dir := FileSystem temp / ('protost-binfile-close-twice-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f g uses refused twice reopened |
 f := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create).
 f close.
 twice := [f close. f close. true] on: Error do: [:e | false].
 uses := {[f append: #[1]]. [f readAt: 0 size: 1]. [f writeAt: 0 bytes: #[1]]. [f size].
          [f truncateTo: 0]. [f sync]. [f dataSync]. [f tryLock]. [f lock]. [f unlock].
          [f append: #[2]]}.
 refused := (uses select: [:use |
   [use value. false] on: Error do: [:e | e messageText includesSubstring: 'BinaryFile is closed']]) size.
 "A new open after the close is a different handle and works; the old one stays closed."
 g := BinaryFile open: (dir / 'seg') pathString options: #(#read #write).
 g append: #[5].
 reopened := [f size. true] on: Error do: [:e | false].
 g close.
 twice printString , ' ' , refused printString , ' ' , f isClosed printString , ' ' , reopened printString]
  ensure: [dir deleteAll].
