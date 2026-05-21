"EXPECT: overridden"
"§4.6: a subclass override wins over the inherited method"
Object subclass: #Base.
Base >> tag
  ^ 'base'.
Base subclass: #Sub.
Sub >> tag
  ^ 'overridden'.
Sub new tag.
