#!/usr/bin/env bash
# REPL (spec §6.2, conformance 58/89; round 3 of the maintainer ruling of
# 2026-11-23): the generated new of a class with constants lives in a hidden
# parent of its metaclass, so a user class-side new is allowed and its
# `super new` is checked. At the REPL a class is redeclared in place, so the
# metaclass that already exists is re-parented:
#   1. a class with a user `^ super new` that gains a constant is checked
#      from then on, and its metaclass holds only the user's new and the
#      generated constructor;
#   2. a subclass with a user `^ super new` whose superclass gains a constant
#      in place is checked too (its metaclass follows the superclass's), and
#      its metaclass's superclass is still the superclass's metaclass; when
#      the superclass drops the constant, it is unchecked again;
#   3. a user new that skips super new is never checked, before or after the
#      class drops its constant;
#   4. the private install path on the metaclass, __installMethod:as:,
#      refuses the generated constructor's selector (M9) and installs new.
set -uo pipefail
PROTOST="$1"
fail() { echo "FAIL ($1): $out"; exit 1; }

# 1.
out="$(printf '%s\n' \
  "Object subclass: #Base2 instanceVariableNames: 'x'." \
  "Base2 class >> new  ^ super new." \
  "Base2 new class." \
  "Object subclass: #Base2 instanceVariableNames: 'x' constantNames: 'b'." \
  "[Base2 new] on: Error do: [:e | e messageText]." \
  "Base2 class selectors asSet = #(#b: #new) asSet." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "=> Base2" || fail "Base2 new before the constant"
echo "$out" | grep -qF "=> the constant 'b' of a Base2 is not set at the end of construction (new)" \
  || fail "Base2's super new is checked after it gains a constant in place"
echo "$out" | grep -qF "=> true" || fail "Base2's metaclass holds no generated new"

# 2.
out="$(printf '%s\n' \
  "Object subclass: #Top instanceVariableNames: 'x'." \
  "Top subclass: #Kid2 instanceVariableNames: 'k'." \
  "Kid2 class >> new  ^ super new." \
  "Kid2 new class." \
  "Object subclass: #Top instanceVariableNames: 'x' constantNames: 't'." \
  "[Kid2 new] on: Error do: [:e | e messageText]." \
  "Kid2 class superclass == Top class." \
  "Object subclass: #Top instanceVariableNames: 'x'." \
  "[Kid2 new] on: Error do: [:e | e messageText]." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "=> the constant 't' of a Kid2 is not set at the end of construction (new)" \
  || fail "Kid2's super new is checked after Top gains a constant in place"
echo "$out" | grep -qF "=> true" || fail "the hidden parent is not Kid2 class superclass"
[ "$(echo "$out" | grep -cF '=> a Kid2')" = "1" ] || fail "Kid2 is unchecked again once Top drops the constant"

# 3.
out="$(printf '%s\n' \
  "Object subclass: #Gone instanceVariableNames: 'h' constantNames: 'g'." \
  "Gone class >> new  ^ 'early'." \
  "Gone new." \
  "Object subclass: #Gone instanceVariableNames: 'h'." \
  "Gone new." | "$PROTOST" -i 2>&1)"
[ "$(echo "$out" | grep -cF '=> early')" = "2" ] || fail "a user new that skips super new is never checked"

# 4.
out="$(printf '%s\n' \
  "Object subclass: #A instanceVariableNames: '' constantNames: 'k'." \
  "A class __installMethod: [:x :y | 'bypass'] as: #k:." \
  "A class __installMethod: [:x | 'mine'] as: #new." \
  "A new." \
  "(A k: 3) class." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -qF "error: k: is the generated constructor of A and cannot be redefined; give your constructor another name (line 1)" \
  || fail "M9 on the metaclass install path (constructor)"
echo "$out" | grep -qF "=> mine" || fail "new installs on the metaclass"
echo "$out" | grep -qF "=> A" || fail "the constructor still builds"
echo "PASS"
