"EXPECT-ERROR: cyclic module import"
"S6 (§11.2): an import cycle — _cycle_a_mod imports _cycle_b_mod, which"
"imports _cycle_a_mod back while its top level is still running — is reported"
"as an error. Loading waits for a module that is still loading, so without"
"cycle detection this import could never finish."
Import from: '_cycle_a_mod'.
