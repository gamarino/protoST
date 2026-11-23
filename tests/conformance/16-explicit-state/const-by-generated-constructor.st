"EXPECT: #(7 7 'ada' true)"
"Spec §6.2, conformance 47: a class with constants gets a class-side keyword
 constructor over them. It writes the constants before initialize runs, so
 initialize reads them freely (seen := number), and answers the instance."
Object subclass: #Account instanceVariableNames: 'seen' constantNames: 'number owner'.
Account >> initialize
  seen := number
Account >> number
  ^ number
Account >> owner
  ^ owner
Account >> seen
  ^ seen

| a |
a := Account number: 7 owner: 'ada'.
{(Account number: 7 owner: 'ada') number. (Account number: 7 owner: 'ada') seen. a owner. a class == Account}
