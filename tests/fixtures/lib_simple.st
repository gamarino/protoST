"-- lib_simple.st: a minimal library module --"
Object subclass: #Greeter.
Greeter >> hello ^ 'hello from lib'.
Greeter >> twice: x ^ x * 2.
