Object subclass: #Animal.
Animal >> describe
  ^ 'an animal'.

Animal subclass: #Dog.
Dog >> describe
  ^ super describe, ' that barks'.

| oc |
oc := OrderedCollection new.
oc add: 1; add: 2; yourself.
oc size printNl.
Dog new describe printNl.
