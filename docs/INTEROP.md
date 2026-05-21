# protoST — Cross-Language Interop Strategy

**Track 5 (see [`docs/ROADMAP.md`](ROADMAP.md)). Sub-slice T5-a complete.**

This document describes how protoST consumes objects and modules produced by
another protoCore runtime — protoJS, protoPython, or any registered
`proto::ModuleProvider` — and the type-mapping rules that make that work. It
also specifies the cross-repository follow-up: the live tri-runtime process
that the protoST repo alone cannot deliver.

---

## 1. Why interop is structurally cheap

Every protoCore runtime — protoCore itself, protoJS, protoPython, protoST —
represents *all* of its values as `proto::ProtoObject`s built from the same
64-byte cell. There is no per-language object representation. A Python object,
a JavaScript object, and a protoST object are the *same kind of thing* at the
kernel level: a cell with a prototype chain and a sparse-list of attributes.

Consequences, which are the whole point of the shared kernel:

- A "foreign" object **is** a `ProtoObject`. protoST needs no wrapper type, no
  proxy, no FFI marshalling layer to hold one.
- A foreign object's *methods* are just *attributes* — a method is a value
  stored on the object (or on a prototype in its chain) under a selector key.
- A protoST message send `obj selector` resolves by walking `obj`'s prototype
  chain with `getAttribute(selectorSymbol)` — exactly the same walk protoCore
  uses for every runtime. So sending a message to a foreign object is not a
  special path; it is the *ordinary* path.

protoST therefore consumes a foreign object the same way it consumes one of
its own: receive it as a `ProtoObject`, send it messages, read its attributes,
put it in collections, close over it in blocks.

---

## 2. How protoST consumes a foreign UMD module

### 2.1 The module-resolution chain

protoCore's Universal Module Discovery (UMD) resolves an `Import` through a
**resolution chain** held on the `ProtoSpace`. The chain is a `ProtoList` of
`ProtoString` specs. Each spec is either:

- `provider:<alias>` or `provider:<guid>` — delegate to a `ModuleProvider`
  registered with the global `proto::ProviderRegistry`; or
- a filesystem path — a `FileSystemProvider` fallback.

`ProtoSpace::getImportModule` walks the chain entry by entry. For a
`provider:` entry it calls `ProviderRegistry::getProviderForSpec`, then
`provider->tryLoad(logicalPath, ctx)`. The first provider to return a non-nil
module wins; the result is cached in the process-global `SharedModuleCache`.

### 2.2 protoST's default chain — and the consumer-side gap

protoST's `STRuntime` constructor sets the resolution chain to **just its own
provider**:

```
["provider:st"]
```

`provider:st` is the `STModuleProvider` (F5 v2) that loads `.st` source
modules. A foreign runtime's provider — say protoPython's, registered with the
`ProviderRegistry` under alias `py` — can be *registered* yet remain
**unreachable from protoST**, because `provider:py` is not in protoST's chain.
UMD never consults a provider whose spec is absent from the chain.

That was the consumer-side gap. It is closed by:

```cpp
void STRuntime::addModuleProviderToChain(const std::string& providerSpec);
```

It appends a `provider:<alias-or-guid>` spec to this space's resolution
chain, **after** `provider:st`, and is idempotent. A host embedding protoST
alongside another runtime calls it once per foreign provider at startup:

```cpp
protoST::STRuntime rt;
// protoPython has registered its provider with the global ProviderRegistry.
rt.addModuleProviderToChain("provider:py");
// Now `Import from: 'some_python_module'` reaches protoPython's provider.
```

Placing the foreign spec *after* `provider:st` means a protoST `.st` module of
the same logical name still shadows a foreign one — protoST's own modules win.

### 2.3 A second consumer-side fix — per-space symbol resolution

`Import from:` (`prim_Import_from`) calls `getImportModule`, which returns a
*wrapper* object carrying the real module under an `exports` attribute, and
unwraps it. The unwrap previously interned the `exports` key in a
function-local `static` — binding it to the **first** runtime's `ProtoSpace`.
Symbols are interned **per `ProtoSpace`**, so in any *later* runtime the stale
key did not match the `exports` attribute that protoCore stamps on the wrapper
in that runtime's space. The unwrap silently missed and returned the bare
wrapper; the next message send (`m SomeClass`) then failed with
`doesNotUnderstand`. The key is now resolved fresh from the live `ctx` on
every call. This matters for any process that constructs more than one
runtime — including a tri-runtime host.

### 2.4 What `Import` yields

`Import from: '<logical-path>'` returns the foreign module as a plain
`ProtoObject`. Its exported classes/objects are attributes; protoST reads them
with ordinary unary sends:

```smalltalk
m := Import from: 'foreign_module'.
w := m Widget.            "unary send → reads the `Widget` attribute"
w doubleIt: 21.           "keyword send → dispatches to a foreign method"
```

---

## 3. The type-mapping strategy

### 3.1 Immediates and strings — shared, zero-conversion

The following are **shared cell types** across every protoCore runtime. A
value of one of these types produced by a foreign runtime *is*, bit for bit,
a protoST value of the same type. No conversion, no copy, no adapter:

| Type | protoCore representation | protoST sees it as |
|------|--------------------------|--------------------|
| Small integer | tagged immediate (`SmallInteger`) | `SmallInteger` |
| Large integer | `LargeInteger` cell | a protoST integer |
| Boolean | `PROTO_TRUE` / `PROTO_FALSE` immediate | `true` / `false` |
| `nil` | `PROTO_NONE` immediate | `nil` |
| Character | tagged immediate | a `Character` |
| Float / double | float cell | a `Float` |
| String | rope of cells (`ProtoString`) | a `String` |

A foreign integer flows straight into protoST arithmetic, a foreign boolean
drives a protoST `ifTrue:ifFalse:`, a foreign string compares equal to a
protoST string literal — all with **no conversion step**:

```smalltalk
m := Import from: 'foreign_module'.
(m answerInt) + 1.                                   "42 + 1 = 43"
(m answerBool) ifTrue: [ 'yes' ] ifFalse: [ 'no' ].  "foreign Boolean"
(m answerString) = 'hello'.                          "foreign String, true"
```

This is exercised directly by `tests/unit/test_t5a_interop.cpp`.

### 3.2 Objects and methods — transparent dispatch

A foreign object's methods are attributes. A protoST message send dispatches
by selector through the shared `getAttribute` walk, so a foreign method is
invoked with no special-casing. Two kinds of foreign method are reachable:

- **Primitive-backed method.** Stored as a tagged-`SmallInteger` marker that
  indexes a native-primitive table. protoST's SEND dispatch recognises the
  marker and calls the primitive. (In the stand-in test the foreign provider
  registers its primitive in the shared primitive registry — see §5.)
- **Attribute-stored value.** A non-callable attribute. A *unary* send reads
  it back as the result (`w label` → the stored value); a non-unary send to a
  value attribute is, correctly, an error.

A foreign object also passes transparently through protoST collections
(`Array`, `OrderedCollection`, …) and through blocks — it is an ordinary
`ProtoObject`, so nothing along those paths needs to know its origin.

### 3.3 Collections — the impedance point

Collections do **not** share a representation the way immediates do. A
Python `list`, a JavaScript `Array`, and a protoST `Array` are three different
*wrapper objects*, each its own runtime's class with its own protocol:

- protoST's `Array` / `OrderedCollection` are objects carrying a backing
  `ProtoList` under a `__data__` attribute, responding to `at:`, `do:`,
  `size`, `collect:`, …
- A foreign collection is *that runtime's* wrapper, responding to *that
  runtime's* protocol (Python's `__getitem__`/`__len__`, JS's `length`/index
  access, …).

A foreign collection handed to protoST is therefore **not** a protoST
`Array`. protoST has two correct ways to consume it:

1. **Send it the foreign protocol.** If protoST code only needs to read
   elements, it can send the foreign collection the messages that runtime's
   collection understands. This works because dispatch is transparent — but
   the protoST code must know the foreign protocol's selector names.
2. **Adapt via an explicit copy.** To treat the data as a first-class protoST
   collection (`do:`, `collect:`, `inject:into:`, species rules), copy the
   foreign elements into a native protoST collection by walking the foreign
   protocol once:

   ```smalltalk
   bag := m Bag.                 "foreign collection wrapper"
   arr := OrderedCollection new.
   arr add: (bag item0).
   arr add: (bag item1).
   arr add: (bag item2).
   "arr is now an ordinary protoST collection."
   ```

The conversion is needed precisely at the point where protoST code wants the
*protoST collection protocol*, not merely element access. A future slice may
provide a reusable `ForeignCollectionAdapter` that wraps a foreign collection
and presents the `SequenceableCollection` protocol on demand; T5-a documents
the boundary rather than building the adapter.

### 3.4 Selector-naming considerations across the three languages

Dispatch is by selector *string*. The three languages name things
differently, so a protoST consumer must use the selector the foreign object
actually carries:

- **protoST / Smalltalk** — keyword selectors carry colons and argument
  structure: `at:put:`, `doubleIt:`. Unary selectors are bare: `size`.
- **protoPython** — method names are plain identifiers: `double_it`,
  `get_item`. A Python method taking one argument is exposed under a
  *single-identifier* selector, not a colon-keyword selector. protoST code
  calls it as a keyword send whose selector text is the bare Python name plus
  a colon only if the bridge chose to colon-decorate it; otherwise the
  protoJS/protoPython bridge decides the exposed selector spelling.
- **protoJS** — method names are plain identifiers too: `doubleIt`,
  `getItem`; camelCase is conventional.

The rule for a protoST consumer: **use the selector the foreign module
documents.** There is no automatic name mangling. When a foreign runtime
publishes a module intended for protoST consumption, its bridge layer is
responsible for choosing protoST-friendly selector spellings (e.g. exposing a
one-argument method under a `name:`-style keyword selector). Establishing a
shared selector-naming convention for published cross-language modules is
itself part of the cross-repo follow-up (§6).

---

## 4. protoST as a *provider* (already done)

The reverse direction — protoST modules consumed by protoJS or protoPython —
already works. F5 v2 registers `STModuleProvider` (alias `st`, GUID
`protoST-source-v1`) with the global `ProviderRegistry`. Any runtime that adds
`provider:st` to its resolution chain can `Import` a protoST `.st` module and
receive its exported classes as `ProtoObject`s. Nothing more is required on
the protoST side for the publish direction.

---

## 5. The stand-in foreign provider (test infrastructure)

protoJS and protoPython are not present in this repository, so the
cross-provider consumption path is verified with a **stand-in foreign
provider** that lives only in the test harness
(`tests/unit/test_t5a_interop.cpp`) — the production runtime is never polluted
with a fake provider.

`FakeForeignProvider` (alias `fake`, GUID `fake-foreign-runtime`) is a
`proto::ModuleProvider` registered with the global `ProviderRegistry`. On
`tryLoad` it builds a `ProtoObject` module deliberately shaped like a foreign
runtime's module: a `Widget` object with a primitive-backed method
(`doubleIt:`) and attribute-stored values (`label`, `version`); a `Bag`
foreign collection wrapper distinct from a protoST `Array`; and bare
immediates (`answerInt` 42, `answerBool` true, `answerString` "hello").

The one genuinely runtime-specific piece — a primitive-backed method — is
registered in the shared primitive registry. In the stand-in the test harness
plays the *coordinating host* that a live tri-runtime process would be (§6):
each runtime registers its own primitives, and the host wires the providers
together.

The tests drive protoST against it and assert: `Import` resolves through the
provider; unary and keyword sends dispatch to foreign methods; a foreign
object survives a round trip through an `OrderedCollection` and through a
block; immediates are used with no conversion; and a foreign collection is
consumed via the foreign protocol (with the adapter-copy sketch shown).

---

## 6. The cross-repo follow-up — the live tri-runtime process

T5-a delivers the protoST-side infrastructure and strategy. The full
end-to-end demonstration — protoJS, protoPython, and protoST all embedded in
**one process, sharing one `ProtoSpace`**, importing each other's *live*
objects — is a separate, cross-repository integration project. This section
specifies precisely what that project needs, so whoever builds it has the
plan.

### 6.1 What the host must do

A single host executable (or library) links all three runtimes' static
libraries. At startup it must, in order:

1. **Create exactly one `ProtoSpace`.** All three runtimes must share it —
   that is what makes a foreign object *be* a usable `ProtoObject` rather than
   something to marshal across a heap boundary. Each runtime must be
   constructed against this shared space rather than each creating its own.
   *(Today `STRuntime`, and the protoJS/protoPython runtime objects, each
   construct their own `ProtoSpace`. Sharing one space requires a small
   constructor change in each runtime to accept an externally-owned
   `ProtoSpace*` — this is the main piece of cross-repo work.)*
2. **Construct each runtime** against that space: the protoJS runtime, the
   protoPython runtime, the protoST `STRuntime`.
3. **Register each runtime's `ModuleProvider`** with the global
   `proto::ProviderRegistry` (protoST already does this for `provider:st`;
   protoJS and protoPython each register theirs).
4. **Wire the resolution chains.** For each runtime, append the *other* two
   runtimes' provider specs to its chain. protoST does this with
   `STRuntime::addModuleProviderToChain("provider:js")` and
   `addModuleProviderToChain("provider:py")`; protoJS and protoPython need an
   equivalent call (a small addition on their side if absent).
5. **Coordinate the primitive registries.** Each runtime owns a primitive
   table. A foreign object's primitive-backed method indexes the *defining*
   runtime's table. Either each runtime keeps its own table and a foreign
   primitive marker is namespaced by runtime, or the host installs a shared
   dispatch table. The exact mechanism is a host-design decision; T5-a's
   stand-in sidesteps it by registering the foreign primitive in protoST's
   own registry — acceptable for a test, not for the real host.

### 6.2 What the demo must show

- A protoPython class imported into protoST, instantiated, and its methods
  invoked as Smalltalk messages.
- A protoJS object imported into protoST, used the same way.
- A protoST class imported into protoPython and protoJS (the publish
  direction, §4).
- Immediates and strings crossing all three boundaries with no conversion.
- A foreign collection consumed via the documented adapter (§3.3).
- One concurrent GC, owned by the shared `ProtoSpace`, correctly tracing
  objects reachable from all three runtimes' roots.

### 6.3 Open items for the host project

- A shared selector-naming convention for published cross-language modules
  (§3.4), so a protoPython method is reachable from protoST under a
  predictable selector.
- The `ForeignCollectionAdapter` (§3.3) — a reusable wrapper presenting the
  protoST `SequenceableCollection` protocol over a foreign collection.
- The primitive-registry coordination mechanism (§6.1, step 5).
- A shared exception-translation policy — a foreign runtime's error object
  reaching a protoST `on: Error do:` handler, and vice versa.

---

## 7. Summary

| Concern | Status |
|---------|--------|
| protoST as a UMD *provider* (publish) | Done — F5 v2 |
| protoST *consuming* a foreign provider's module | Done — T5-a; `addModuleProviderToChain` |
| Per-space symbol resolution in `Import` unwrap | Fixed — T5-a |
| Immediates / strings cross with no conversion | Verified — T5-a tests |
| Foreign object message dispatch (unary + keyword) | Verified — T5-a tests |
| Foreign object through collections and blocks | Verified — T5-a tests |
| Foreign collection impedance + adapter boundary | Documented — §3.3 |
| Live tri-runtime process | Specified — §6; cross-repo follow-up |
