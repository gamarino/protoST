"EXPECT: FileSystemError FileSystemError"
"FileReference>>realPath signals FileSystemError for a symbolic link loop
 (Python's non-strict os.path.realpath would keep the link unresolved; a lock
 named after such a path would not protect anything). Links are made with
 ln -s, or mklink on Windows, which needs a privilege or developer mode:
 without it the test reports SKIP."
| dir link made a b |
dir := FileSystem temp / ('protost-files-realpath-loop-' , Smalltalk pid printString).
dir ensureCreateDirectory.
link := [:target :name | | r |
  r := Smalltalk platform = 'windows'
    ifTrue: [OSProcess shell: 'mklink "' , ((dir / name) pathString copyReplaceAll: '/' with: '\') , '" "' , target , '"']
    ifFalse: [OSProcess run: 'ln' arguments: (Array with: '-s' with: target with: (dir / name) pathString)].
  r succeeded].
[made := (link value: 'b' value: 'a') and: [link value: 'a' value: 'b'].
 made ifTrue: [
   a := [(dir / 'a') realPath. 'no error'] on: FileSystemError do: [:e | e class name].
   b := [(dir / 'a' / 'x.log') realPath. 'no error'] on: FileSystemError do: [:e | e class name]]]
  ensure: [dir deleteAll].
made
  ifTrue: [a , ' ' , b]
  ifFalse: ['SKIP: this process may not create symbolic links (Windows needs a privilege or developer mode)'].
