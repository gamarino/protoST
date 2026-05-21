"EXPECT: base"
"§4: T3-a — `super` inside a MODULE's own method resolves correctly after the
module is imported. ModuleLabel>>label calls `super label`; neither
ModuleLabel nor its parent BaseLabel is a global of this importing program.
`super` resolves the defining class by walking the receiver's prototype chain
by object identity, so the cross-module parent (BaseLabel) is reached."
lib := Import from: '_extensible_counter'.
m := lib ModuleLabel newChild.
m label.
