# Advanced Object Model — Design Spec

**Track 3** (see `docs/ROADMAP.md`). The showcase track — it exposes
object-model capabilities that go *beyond* standard Smalltalk, directly
demonstrating what protoCore enables.

## Goal

Three capabilities, all leaning on protoCore's prototype model (which already
supports an object having **multiple parents** — `addParent`, `setParents`,
`getParents`):

1. **Extensible classes from modules** — import a class from a module,
   subclass it locally, override a method, and use `super` to reuse the
   module's original implementation.
2. **Multiple inheritance / mixins** — a class may have several superclasses
   / mixin behaviours; method lookup walks them in a defined order.
3. **On-the-fly behaviour** — add a behaviour (a parent) to an existing class
   at runtime; every instance gains it.

## What exists today

- Single inheritance: `Object subclass: #Foo …` gives `Foo` one parent.
- `super` works (BL-1): a method's owning class is recorded on its bytecode
  module; a `super` send resolves starting at that class's *first* parent.
- protoCore objects can have multiple parents — `addParent` / `setParents` /
  `getParents` — and attribute/method lookup walks the parent list. protoST
  has simply not exposed this at the language level.
- Modules (F5/F5v2) load `.st` files and expose their classes.

## Sub-slices

### T3-a — Extensible classes from modules

`Import from: 'lib'` exposes a module's classes. A program must be able to:
- subclass an imported class: `(lib Counter) subclass: #FastCounter …`;
- override a method in the subclass;
- call `super` in the override to reuse the module class's implementation.

Investigate how much already works (`super` + modules exist). The deliverable:
the import → subclass → override → `super` path works end to end, with tests,
and is documented as a first-class pattern. Fix whatever gaps block it (likely
`subclass:` accepting an arbitrary class object as the superclass, and `super`
resolving correctly when the defining class's parent lives in another module).

### T3-b — Multiple inheritance / mixins

A class may be defined with several superclasses / mixins. A mixin is not a
separate kind — it is just a class (or any object carrying methods); "mixing
in" means adding it as an additional parent. Surface this:

- A definition form for multiple parents — e.g.
  `Object subclass: #Foo uses: { MixinA. MixinB }` (the `uses:` keyword takes
  a literal array / collection of class objects to add as extra parents,
  alongside the primary superclass). Pick the cleanest syntax that the parser
  already supports or that needs a minimal grammar addition; document it.
- Method/attribute lookup walks the parents. **Resolution order**: define and
  document it — depth-first, left-to-right over `getParents` (the primary
  superclass first, then the `uses:` mixins in order), consistent with
  whatever protoCore's lookup already does. Document the diamond case: a
  selector found via two paths resolves to the first in this order.
- `super` from a method of a multiply-inheriting class starts the search at
  the next class in this order after the method's owning class.

### T3-c — On-the-fly behaviour

Add behaviour to an existing class at runtime:
- `aClass addBehavior: aMixin` (or `aClass addParent: aClass2`) — adds a
  parent to the class object; every existing and future instance immediately
  responds to the mixin's methods (protoCore's `addParent` makes this live).
- Removing a behaviour is optional (`removeBehavior:` — only if protoCore's
  parent API supports removal cleanly; otherwise note it as out of scope).

This is the most direct "look what the prototype kernel allows" demonstration
— behaviour composition with no recompilation.

## Constraints / notes

- Minimal decoration over protoCore: lookup, the parent list, and the walk are
  protoCore's — protoST only adds the surface syntax (`uses:`, `addBehavior:`)
  and wires it to `addParent`/`setParents`.
- `super` already uses a compiler-embedded "defining class". With multiple
  parents the embedded class is still the *defining* class; only the *next*
  step (which parent to search) generalises — from "the first parent" to "walk
  the resolution order after the defining class". Keep `super` correct.
- Instance-variable layout: a class assembled from multiple parents — each
  parent may declare instance variables. Decide how instance variables
  combine (union of the parents' ivars). If this gets deep, the MVP can
  restrict mixins to behaviour-only (methods, no new ivars) and document that
  — but try to support combined ivars if protoCore's model makes it natural.

## Tests

Per sub-slice:
- T3-a: a module class subclassed in the importing program; an override that
  calls `super` and observably reuses the module implementation.
- T3-b: a class with two mixins, each contributing a method, both callable;
  a diamond resolved per the documented order; `super` across the order.
- T3-c: `addBehavior:` on an existing class; an instance created *before* the
  add responds to the new method afterwards.
- Regression: the existing suite stays green; single inheritance unaffected.

Update `docs/LANGUAGE.md` (object-model section) and `docs/STATUS.md` as each
sub-slice lands.
