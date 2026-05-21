"EXPECT: 1"
"§11.2: Import from: loads a module and returns the module object;
_counter_mod exports the Counter class. The runner executes a test with its
own directory as the working directory, so the bare name resolves."
m := Import from: '_counter_mod'.
c := m Counter new.
c initialize.
c increment.
c value.
