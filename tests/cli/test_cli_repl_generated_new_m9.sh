#!/usr/bin/env bash
# REPL (maintainer ruling of 2026-11-23, M9 for the generated new): a class
# with constants has a generated class-side new that runs super new and then
# the end-of-construction check, and no user new may take its place. At the
# REPL a class is redeclared in place, so the refusal is made at the
# declaration, signalled in place, before anything is recorded:
#   1. a class with a user new cannot gain a constant;
#   2. nor can the superclass of a class with a user new;
# in both cases the class keeps its previous declaration and the user new.
#   3. A user new written for a class with constants is refused at compile
#      time; once the class drops its constant in place, the generated new is
#      gone (the metaclass holds no new) and a user new installs and survives
#      a further redeclaration without constants.
#   4. The private install path on the metaclass, __installMethod:as:, is
#      refused like __installClassMethod:as:, for new and for the generated
#      constructor: no install bypasses the check.
set -uo pipefail
PROTOST="$1"
fail() { echo "FAIL ($1): $out"; exit 1; }
text() { echo "new is generated for $1, which has constants: it runs super new and then checks them, and cannot be redefined; give your constructor another name"; }

# 1. the class itself gains a constant.
out="$(printf '%s\n' \
  "Object subclass: #Base2 instanceVariableNames: 'x'." \
  "Base2 class >> new  ^ 'user new'." \
  "Object subclass: #Base2 instanceVariableNames: 'x' constantNames: 'b'." \
  "Base2 new." \
  "Base2 instVarModifierOf: #b." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "error: $(text Base2)" || fail "M9 at the declaration of Base2"
echo "$out" | grep -qF "=> user new" || fail "Base2 keeps its user new"
echo "$out" | grep -qF "error: Base2 has no instance variable 'b'" || fail "Base2 keeps its previous declaration"

# 2. the superclass gains a constant; the subclass has a user new.
out="$(printf '%s\n' \
  "Object subclass: #Top instanceVariableNames: 'x'." \
  "Top subclass: #Kid2 instanceVariableNames: 'k'." \
  "Kid2 class >> new  ^ 'kid new'." \
  "Object subclass: #Top instanceVariableNames: 'x' constantNames: 't'." \
  "Kid2 new." \
  "Top new instVarIsSet: #x." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "error: $(text Kid2)" || fail "M9 at the declaration of Top, for Kid2"
echo "$out" | grep -qF "=> kid new" || fail "Kid2 keeps its user new"
echo "$out" | grep -qF "=> false" || fail "Top keeps its previous declaration"

# 3. compile-time refusal; stale removal keeps a user new.
out="$(printf '%s\n' \
  "Object subclass: #Gone instanceVariableNames: 'h' constantNames: 'g'." \
  "Gone class >> new  ^ 'early'." \
  "Object subclass: #Gone instanceVariableNames: 'h'." \
  "Gone class selectors." \
  "Gone class >> new  ^ 'late'." \
  "Object subclass: #Gone instanceVariableNames: 'h' writeOnceNames: 'w'." \
  "Gone new." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "compile error: $(text Gone) (line 1)" || fail "compile-time M9 on Gone"
echo "$out" | grep -qF "=> #()" || fail "the stale generated new is removed"
echo "$out" | grep -qF "=> late" || fail "the user new survives the redeclaration"
# 4. the metaclass install path.
out="$(printf '%s\n' \
  "Object subclass: #A instanceVariableNames: '' constantNames: 'k'." \
  "A class __installMethod: [:x | 'bypass'] as: #new." \
  "A class __installMethod: [:x :y | 'bypass'] as: #k:." \
  "[A new] on: Error do: [:e | e messageText]." \
  "(A k: 3) class." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "error: $(text A) (line 1)" || fail "M9 on the metaclass install path (new)"
echo "$out" | grep -qF "error: k: is the generated constructor of A and cannot be redefined; give your constructor another name (line 1)" \
  || fail "M9 on the metaclass install path (constructor)"
echo "$out" | grep -qF "=> the constant 'k' of an A is not set at the end of construction (new)" || fail "new still checks"
echo "$out" | grep -qF "=> A" || fail "the constructor still builds"
echo "PASS"
