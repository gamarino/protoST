"EXPECT-ERROR: error: instance variable 'secret' of Kid is already defined in its superclass LateBase"
"§3.2 (S37): a subclass of an imported class may not redeclare an instance
variable it inherits. The error is the declaration's, signalled when the
class is built; it was once reported as a compile error of the imported
module, which failed the import instead."
m := Import from: '_late_base_mod'.
LateBase subclass: #Kid instanceVariableNames: 'own secret'.
3.
