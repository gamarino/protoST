#!/usr/bin/env bash
# REPL (explicit-state spec §2 anti-goal, §6.1, final-review m3): a class
# redeclared in place keeps its methods. A redeclaration that brings back a
# variable an earlier declaration dropped, with another modifier, must not
# leave a method compiled for the old modifier able to write or read it. The
# redeclaration re-checks the installed methods of the class and of its
# subclasses that use the variable, with the rule a method installed late
# follows (M1, M2, M4), and is refused, signalled in place, when one of them
# no longer fits; the class then keeps its previous declaration. A script
# file refuses the same program at compile time. Cases 6 and 7 are the same
# hazard from above (a superclass or mixin method; final re-review).
set -uo pipefail
PROTOST="$1"
fail() { echo "FAIL ($1): $out"; exit 1; }

# 1. plain -> dropped -> constant: the plain store outside initialize (M1).
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'a'." \
  "Foo >> seta: x  a := x." \
  "Foo >> a  ^ a." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: '' constantNames: 'a'." \
  "Foo instVarNames." \
  "Foo instVarModifierOf: #b." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: cannot assign to the constant 'a' of Foo outside initialize (line 1); Foo >> seta: was compiled when 'a' was plain: redefine it first, then declare Foo again" \
  || fail "M1 at the redeclaration"
echo "$out" | grep -q "=> #('b')" || fail "the previous declaration is kept"
echo "$out" | grep -q "=> plain" || fail "the previous modifiers are kept"

# 2. plain -> dropped -> atomic, the store in a subclass method (M4).
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'a'." \
  "Foo subclass: #Sub instanceVariableNames: 's'." \
  "Sub >> seta: x  a := x." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: '' atomicNames: 'a'." \
  "Foo instVarNames." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: cannot assign to the atomic variable 'a' of Sub: use update:with: or compareAndSet:from:to: (line 1); Sub >> seta: was compiled when 'a' was plain: redefine it first, then declare Foo again" \
  || fail "M4 for a subclass method"
echo "$out" | grep -q "=> #('b')" || fail "the previous declaration is kept (M4)"

# 3. plain -> dropped -> constant, the store in initialize: the source is
#    allowed, but the method was compiled for a plain variable. Redefined
#    without the variable, the redeclaration is accepted, and initialize
#    compiled again sets the constant.
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'a'." \
  "Foo >> initialize  a := 1." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: '' constantNames: 'a'." \
  "Foo >> initialize  ^ self." \
  "Object subclass: #Foo instanceVariableNames: '' constantNames: 'a'." \
  "Foo >> initialize  a := 1." \
  "Foo new instVarIsSet: #a." \
  "Foo instVarModifierOf: #a." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: Foo >> initialize was compiled when 'a' was plain and does not fit 'a' constant: redefine it first, then declare Foo again" \
  || fail "a stale initialize"
[ "$(echo "$out" | grep -c '^protoST> error')" -eq 1 ] || fail "only the first redeclaration is refused"
echo "$out" | grep -q "=> true" || fail "initialize compiled again sets the constant"
echo "$out" | grep -q "=> constant" || fail "the second redeclaration took effect"

# 4. constant -> dropped -> plain: the constant store and the checked read
#    compiled for the constant each no longer fit a plain variable.
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: '' constantNames: 'a'." \
  "Foo >> initialize  a := 1." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: 'a'." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: Foo >> initialize was compiled when 'a' was constant and does not fit 'a' plain" \
  || fail "a stale constant store"
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: '' constantNames: 'a'." \
  "Foo >> a  ^ a." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: 'a'." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: Foo >> a was compiled when 'a' was constant and does not fit 'a' plain" \
  || fail "a stale checked read"

# 5. A variable brought back with the modifier its methods were compiled for
#    is accepted, as before.
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'a'." \
  "Foo >> seta: x  a := x." \
  "Foo >> a  ^ a." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Object subclass: #Foo instanceVariableNames: 'a b'." \
  "(Foo new seta: 7; yourself) a." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error" && fail "a plain variable brought back plain"
echo "$out" | grep -q "=> 7" || fail "the method still works"

# 6. From above (final re-review, New Issue 1): a superclass redeclared
#    without 'a' keeps a method compiled when it declared 'a' plain; a new
#    subclass that declares 'a' constant is refused, so the stale store cannot
#    overwrite a set constant. Redefined, the declaration is accepted.
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'a'." \
  "Foo >> seta: x  a := x." \
  "Object subclass: #Foo instanceVariableNames: 'b'." \
  "Foo subclass: #Bar instanceVariableNames: '' constantNames: 'a'." \
  "Smalltalk includesKey: #Bar." \
  "Foo >> seta: x  ^ x." \
  "Foo subclass: #Bar instanceVariableNames: '' constantNames: 'a'." \
  "Bar >> initialize  a := 1." \
  "Bar new instVarIsSet: #a." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: cannot assign to the constant 'a' of Foo outside initialize (line 1); Foo >> seta: was compiled when 'a' was plain: redefine it first, then declare Bar again" \
  || fail "a stale superclass store"
echo "$out" | grep -q "=> false" || fail "the refused subclass is not bound"
echo "$out" | grep -q "=> true" || fail "the subclass is accepted once the method is redefined"

# 7. The same through addBehavior: a mixin's stale store on a name the class
#    declares constant is refused; a mixin with no stale method is accepted.
out="$(printf '%s\n' \
  "Object subclass: #Mx instanceVariableNames: 'a'." \
  "Mx >> seta: x  a := x." \
  "Object subclass: #Mx instanceVariableNames: 'b'." \
  "Object subclass: #K instanceVariableNames: '' constantNames: 'a'." \
  "K >> initialize  a := 1." \
  "K addBehavior: Mx." \
  "K new respondsTo: #seta:." \
  "Object subclass: #Ok instanceVariableNames: 'z'." \
  "Ok >> setz: x  z := x." \
  "K addBehavior: Ok." \
  "K new respondsTo: #setz:." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "error: cannot assign to the constant 'a' of Mx outside initialize (line 1); Mx >> seta: was compiled when 'a' was plain" \
  || fail "a stale mixin store"
echo "$out" | grep -q "=> false" || fail "the refused mixin is not added"
echo "$out" | grep -q "=> true" || fail "a mixin without a stale method is added"
echo PASS
