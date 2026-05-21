"EXPECT: sub-x"
"§5.3: in a super-send, self is still the original receiver"
Object subclass: #Base.
Base >> name
  ^ 'base'.
Base >> describe
  ^ self name.
Base subclass: #Sub.
Sub >> name
  ^ 'sub-x'.
Sub >> describe
  ^ super describe.
Sub new describe.
