"EXPECT: local/module/base"
"A super send starts at the parent of the class that defines the running
 method, found on the receiver's chain by identity. Subclassing an imported
 class under its own name puts two classes named Figure on the chain; matching
 by name made the imported Figure's super find the local one, and the send
 recursed until the stack depth was exceeded."
m := Import from: '_homonym_base_mod'.
m Figure subclass: #Figure.
Figure >> label  ^ 'local/' , super label.

Figure new label.
