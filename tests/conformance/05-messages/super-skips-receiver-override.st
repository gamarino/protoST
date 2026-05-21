"EXPECT: base-impl"
"§5.3: super lookup begins at the parent of the defining class,
skipping any override on the receiver"
Object subclass: #Base.
Base >> work
  ^ 'base-impl'.
Base subclass: #Sub.
Sub >> work
  ^ 'sub-impl'.
Sub >> viaSuper
  ^ super work.
Sub new viaSuper.
