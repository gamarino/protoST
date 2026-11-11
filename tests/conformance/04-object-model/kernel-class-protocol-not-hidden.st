"EXPECT: #()"
"§4.7, K14, D52: a class's class-side protocol lives on its metaclass and a
 send to the class never reaches the class's own instance side. A built-in
 constructor bound on the instance side by mistake would therefore be
 invisible to the class, the superclass's class-side method answering
 instead (Collection class >> new: for Array new:). Every global class is
 checked: no selector it defines on the instance side only is also defined
 on the class side of one of its superclasses. Behavior and its subclasses
 are left out: their instance side is the protocol of classes, which is
 meant to sit beside Object class's."
| hidden |
hidden := OrderedCollection new.
Smalltalk allClasses do: [:sub |
  | own ownClassSide |
  own := sub selectors.
  ownClassSide := sub class selectors.
  (sub == Behavior or: [sub inheritsFrom: Behavior]) ifFalse: [
    sub allSuperclasses do: [:sup |
      sup class selectors do: [:s |
        ((own includes: s) and: [(ownClassSide includes: s) not]) ifTrue: [
          hidden add: sup name , ' class >> ' , s , ' hides ' , sub name , ' >> ' , s]]]]].
hidden asArray
