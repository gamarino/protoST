"EXPECT-ERROR: 'secret' is already defined as an instance variable of Kid"
"§3.3 (S37): a temporary may not reuse the name of an instance variable the
class inherits from an imported class."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid.

Kid >> f
  | secret |
  secret := 1.
  ^ secret.

3.
