"EXPECT: 1 1 2 2 2 #[7] 1 1 0"
"BinaryFile>>linkCount and FileReference>>linkCount: one name; two after a
 hard link (ln; mklink /H on Windows), read from the open file and by either
 path; one again after the second name is deleted. The open file's count
 follows the file it opened: with its last name deleted it answers 0 (POSIX;
 Windows may keep counting a name whose deletion waits for the last handle,
 so there the last value is printed as 0 without asking)."
| dir log alias f r gone counts |
dir := FileSystem temp / ('protost-binfile-links-' , Smalltalk pid printString).
dir ensureCreateDirectory.
log := dir / 'log'.
alias := dir / 'alias'.
counts := OrderedCollection new.
[f := BinaryFile open: log options: #(#read #write #create #exclusive).
 f append: #[7].
 counts add: f linkCount; add: log linkCount.
 r := Smalltalk platform = 'windows'
   ifTrue: [OSProcess shell: 'mklink /H "' , (alias pathString copyReplaceAll: '/' with: '\') , '" "'
                               , (log pathString copyReplaceAll: '/' with: '\') , '"']
   ifFalse: [OSProcess run: 'ln' arguments: (Array with: log pathString with: alias pathString)].
 r succeeded ifFalse: [Error signal: 'hard link failed: ' , r output , r errorOutput].
 counts add: f linkCount; add: log linkCount; add: alias linkCount.
 counts add: alias binaryContents.
 alias delete.
 counts add: f linkCount; add: log linkCount.
 Smalltalk platform = 'windows'
   ifTrue: [gone := 0]
   ifFalse: [log delete. gone := f linkCount].
 counts add: gone.
 f close]
  ensure: [dir deleteAll].
(counts collect: [:each | each printString]) inject: '' into: [:acc :each |
  acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]].
