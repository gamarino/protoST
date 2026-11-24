"EXPECT: #(true #() 'MessageNotUnderstood' true 'the generated constructor number:owner:extra: of Extra is out of date: a class it inherits from changed its constants; redeclare Extra' true)"
"Spec §6.2, Task 6 ruling I2: a subclass that adds no constant owns no copy
 of the constructor, so redeclaring its superclass in place leaves nothing
 stale on it: the old selector is gone (not understood, never a private
 text), and the new one is inherited and guarded by M9. A subclass with
 constants of its own keeps a constructor the runtime cannot regenerate;
 calling it signals an Error naming the class to redeclare."
Object subclass: #Account instanceVariableNames: 'x' constantNames: 'number owner'.
Account subclass: #Sub instanceVariableNames: 'y'.
Account subclass: #Extra instanceVariableNames: '' constantNames: 'extra'.

| old r |
old := Account.
Object subclass: #Account instanceVariableNames: 'x' constantNames: 'number'.
{old == Account.
 Sub class selectors asArray.
 [Sub number: 1 owner: 2. 'built'] on: MessageNotUnderstood do: [:e | e class name].
 (Sub number: 1) class == Sub.
 [Extra number: 1 owner: 2 extra: 3. 'built'] on: Error do: [:e | e messageText].
 [Import from: '_sub_number'. 'installed'] on: Error do: [:e |
   e messageText includesSubstring: 'number: is the generated constructor of Sub and cannot be redefined']}
