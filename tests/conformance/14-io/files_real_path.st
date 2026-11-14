"EXPECT: true true true true true true true true false true"
"FileReference>>realPath: symbolic links resolved, the missing tail appended
 as written, as Python's os.path.realpath (non-strict) does: a plain file; a
 symbolic link to it (the lock name is then the same by both names); a
 symbolically linked directory in the middle of a path; a missing tail under
 it; a dangling link, followed to its target; the directory itself; nothing
 is created; it answers a FileReference. Links are made with ln -s, or mklink
 (/D for a directory) on Windows, which needs a privilege or developer mode:
 without it the test reports SKIP."
| dir base results link |
dir := FileSystem temp / ('protost-files-realpath-' , Smalltalk pid printString).
dir ensureCreateDirectory.
link := [:target :name :isDir | | r |
  r := Smalltalk platform = 'windows'
    ifTrue: [OSProcess shell: 'mklink ' , (isDir ifTrue: ['/D '] ifFalse: ['']) , '"'
                               , ((dir / name) pathString copyReplaceAll: '/' with: '\') , '" "'
                               , (target copyReplaceAll: '/' with: '\') , '"']
    ifFalse: [OSProcess run: 'ln' arguments: (Array with: '-s' with: target with: (dir / name) pathString)].
  r succeeded].
results := [
  base := dir realPath pathString.
  (dir / 'log') contents: 'x'.
  (dir / 'real') createDirectory.
  (dir / 'real' / 'sub') createDirectory.
  ((link value: 'log' value: 'log-link' value: false)
     and: [(link value: 'real' value: 'ldir' value: true)
     and: [link value: 'target.log' value: 'dangling' value: false]])
    ifFalse: [nil]
    ifTrue: [
      OrderedCollection new
        add: (dir / 'log') realPath pathString = (base , '/log');
        add: (dir / 'log-link') realPath pathString = (base , '/log');
        add: ((dir / 'log-link') realPath pathString , '.lock') = ((dir / 'log') realPath pathString , '.lock');
        add: (dir / 'ldir' / 'sub') realPath pathString = (base , '/real/sub');
        add: (dir / 'ldir' / 'new' / 'x.log') realPath pathString = (base , '/real/new/x.log');
        add: (dir / 'dangling') realPath pathString = (base , '/target.log');
        add: ((FileReference path: dir pathString , '/missing/../log') realPath pathString = (base , '/log'));
        add: (dir realPath = dir realPath realPath);
        add: (dir / 'ldir' / 'new') exists;
        add: ((dir / 'log') realPath isKindOf: FileReference);
        yourself]]
  ensure: [dir deleteAll].
results isNil
  ifTrue: ['SKIP: this process may not create symbolic links (Windows needs a privilege or developer mode)']
  ifFalse: [(results collect: [:each | each printString]) inject: '' into: [:acc :each |
    acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]]].
