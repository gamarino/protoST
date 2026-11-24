"EXPECT: #(1 'x')"
"Spec §6.2, conformance 48: new stays available; an initialize that sets
 every constant passes the end-of-construction check."
Object subclass: #Account instanceVariableNames: '' constantNames: 'number owner'.
Account >> initialize
  number := 1.
  owner := 'x'
Account >> number
  ^ number
Account >> owner
  ^ owner

| a |
a := Account new.
{a number. a owner}
