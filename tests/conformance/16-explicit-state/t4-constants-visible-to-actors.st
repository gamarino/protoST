"EXPECT: 1000"
"Spec §14.3 T4: constants given to the generated constructor are read
 correctly by an actor that receives the object right after construction.
 1,000 Accounts are built with Account number: i owner: ... and each is sent
 at once to one of 4 reader actors, which answers its number and owner.
 Prints the number of correct answers. Checked by count only."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> number
  ^ number
Account >> owner
  ^ owner

Object subclass: #Reader instanceVariableNames: ''.
Reader >> read: anAccount
  ^ {anAccount number. anAccount owner}

| readers futures owners correct |
readers := (1 to: 4) collect: [:i | Reader new asActor].
owners := (1 to: 1000) collect: [:i | 'owner-' , i printString].
futures := (1 to: 1000) collect: [:i |
  (readers at: i \\ 4 + 1) read: (Account number: i owner: (owners at: i))].
correct := 0.
futures doWithIndex: [:f :i | | r |
  r := f wait.
  ((r at: 1) = i and: [(r at: 2) == (owners at: i)]) ifTrue: [correct := correct + 1]].
correct
