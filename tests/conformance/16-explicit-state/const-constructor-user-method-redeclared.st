"EXPECT: #('number: is the generated constructor of Account and cannot be redefined; give your constructor another name' true 'user number:' 'no M13')"
"Spec §6.2, M9, Task 6 ruling I1: an in-place redeclaration that would make
 a user class method's selector the generated constructor is refused with
 M9 before anything is recorded. The method comes from another module, so
 the compiler cannot see it; the declaration is refused when it runs, and
 the class is left as it was: no constant, the user's method in place."
| old refused |
Import from: '_ctor_user_number'.
old := Account.
refused := [Import from: '_ctor_redeclare_number'. 'redeclared'] on: Error do: [:e | e messageText].
{refused. old == Account. Account number: 1. [Account new. 'no M13'] on: Error do: [:e | e messageText]}
