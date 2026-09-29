"EXPECT: 3"
"A method whose last statement has no period ends at the next declaration,
 even without a blank line in between."
Object subclass: #T.
T >> a ^ 1
T >> b ^ 2
T class >> c ^ 0

T new a + T new b + T c
