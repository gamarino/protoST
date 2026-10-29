"Module for super-resolves-by-identity-not-name.st: a class whose method sends
 super, which the importer subclasses under the same name."
Object subclass: #Base.
Base >> label  ^ 'base'.

Base subclass: #Figure.
Figure >> label  ^ 'module/' , super label.
