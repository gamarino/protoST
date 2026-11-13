"EXPECT: #(#userRem #userRem 3 'LargeInteger')"
"rem: is not one of Pharo's special selectors, so it has no engine fast path: a method a program adds to SmallInteger for it is reached by an ordinary send, as by perform: (review R3-1). bitAnd: is one, and keeps the primitive answer."
SmallInteger >> rem: aNumber ^ #userRem.
SmallInteger >> bitAnd: aNumber ^ #userAnd.
{ 7 rem: 2. 7 perform: #rem: with: 2. 7 bitAnd: 3. (4503599627370496 * 4) class name }
