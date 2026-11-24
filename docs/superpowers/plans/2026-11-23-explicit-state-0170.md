# protoST 0.17.0, explicit state I: instance-variable modifiers — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship protoST 0.17.0, the first release of the explicit-state spec. Mutable classes gain `constantNames:`, `writeOnceNames:` and `atomicNames:`, with:
- `compareAndSet:from:to:` and `update:with:`;
- the end-of-construction check (M13);
- the checked constant read (`PUSH_CONST_INSTVAR`, `UnsetConstantError`, M14);
- `instVarIsSet:` (M15);
- `setInstVar:from:to:` deprecated (W1);
- the §14.1 performance gate.

**Architecture:**
- **Declaration.** The modifiers are recorded on the class object when the declaration runs. The textual form and the message form share one runtime path, `declareInstVarNames`.
- **Compiler and install time.** The compiler reads the modifiers from its class registry when it can see the class. Install-time resolution (`resolveLateVariables`) reads them from the class object otherwise.
- **Opcodes.** Every new check sits on a new opcode or a primitive: `STORE_CONST_INSTVAR`, `STORE_ONCE_INSTVAR` and `PUSH_CONST_INSTVAR`, plus the new primitives (`state_prims.cpp`). `PUSH_INSTVAR`, `STORE_INSTVAR` and `IVAR_GROUP*` gain no work.

**Tech Stack:**
- C++20, protoCore 3.6.1 public API (`setAttributeIfEqual`, `setAttributes`, `getOwnAttributeDirect`), protoIO 0.7.1;
- Catch2 unit tests, the conformance runner (`tests/conformance/run_conformance.sh`), CLI shell tests;
- `tests/docs/run_doc_snippets.py`, `docs/vm-examples/disasm_examples.py`;
- `perf stat`.

**Spec:** `docs/superpowers/specs/2026-11-23-explicit-state-design.md` (approved 2026-11-23). Section numbers below (§6.2, M13, test 61…) are the spec's. Read §6, §11, §12, §13.1, §14 and §15 before any task.

## Global Constraints

- **Scope:** 0.17.0 only, the first row of spec §15. Value classes, `valueSubclass:`, `derivedNames:`, `isValueClass`, `#field`, V1–V17, conformance 1–46, 86 and 103, T3 and T6 are 0.18.0. Do not implement them.
- **protoCore:** floor 3.6.1, unchanged (`PROTOCORE_MIN_VERSION "3.6.1"`). Make **no protoCore change**. Use the **public API only**: never include `proto_internal.h`, and never hand-copy pointer tags (`tests/cli/test_src_public_api_only.sh` guards this).
- **protoIO:** floor 0.7.1, unchanged.
- **Errors** are C++ exceptions, surfaced as Smalltalk exceptions. There is no pending-error flag, and a CAS retry is an ordinary loop.
- **Threads:**
  - no `std::counting_semaphore` (S19);
  - no native threads (`std::thread`, `pthread_*`) in runtime code;
  - threads only through ProtoThread APIs;
  - no mutex held across a protoCore allocation;
  - no lock anywhere in the new paths.
- **GC:**
  - no new root sets, and no edits to the collector loop;
  - transients are rooted on the operand stack, in frame slots or under `TransientPin`;
  - a handler that keeps a `TransientPin` alive ends with `DISPATCH_SCOPED()`, never `DISPATCH_DIRECT()` (S25);
  - no `ProtoTuple` for fields.
- **Fast path:** `PUSH_INSTVAR`, `STORE_INSTVAR`, `PUSH_OWN_INSTVAR`, `IVAR_GROUP` and `IVAR_GROUP_END` handlers keep their instruction sequence. The only exception is the error class on the immutable-receiver branch (Task 8), which is not on the fast path.
- **Correctness comes before speed.** No safety check may be narrowed for performance.
- **Tests:**
  - no timing-dependent test: no sleeps used as synchronization, and no wall-clock assertions;
  - every new behaviour gets a fail-first test, run red before the code exists;
  - run tests sequentially (`ctest` without `-j`);
  - benchmarks run serially on a quiet machine with `perf stat -r 3 -e cycles`, with a fresh copy of the binary and library per run;
  - a benchmark self-reports its work, and the runner verifies it.
- **Pinned texts** (spec §12) are copied verbatim, including the quoted names:
  - M14: `constant 'owner' of an Account read before it was set`.
  - M15: `instVarIsSet: an Account has no instance variable 'ownr'`, and `instVarIsSet: expects a String or Symbol, not 3`.
  - W1: `protoST: warning: setInstVar:from:to: is deprecated and is removed in protoST 0.18.0; use compareAndSet:from:to:`
  - Every other M text: the §12 wording. The phrases *already set*, *atomic variable*, *must be pure*, *generated constructor*, *already holds a different value* and *not set at the end of construction* are pinned.
  - Compile-time and install-time errors append ` (line N)` to the text, as the compiler's existing errors do.
- **Language:** professional English in code, comments, docs and commit messages. Each commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Paths:**
  - worktree `WT=/home/gamarino/Documentos/proyectos/worktrees/protost-0170`, branch `feat/explicit-state-0170`;
  - scratch `S=/tmp/claude-1000/-home-gamarino-Documentos-proyectos/c08c626b-89d4-4a1e-b668-5ae9b9051093/scratchpad/plan0170`.
  - **No `rm` in any form, no `git clean`, no `pkill`.** A superseded scratch directory is left in place, and a new one is made under a new name.
- **protoBaseST (follow-up, not a task):** Q5's write-once equality rule (M12) changes which of protoBaseST's `_publish…:` patterns stay valid under `:=` (spec §13.2). protoBaseST 0.4.x keeps running on 0.17.0 with warning W1. Its migration is protoBaseST 0.5.0, after protoST 0.18.0. Mention it in the CHANGELOG; change nothing in protoBaseST.

## Decisions this plan makes (spec §16, "left to the builder", and gaps)

- **D-a, marks storage.** Three attributes on the class object:
  - `__ivar_modifiers__`: a String of `kind:name` words for the class's **own** non-plain variables, with kind `constant`, `writeOnce` or `atomic` (for example `'constant:number constant:owner writeOnce:oid atomic:balance'`);
  - `__has_constants__`: `true` on a class that declares or inherits a constant, read with `getAttribute`, so subclasses see it through the chain;
  - `__constant_constructor__`: the Symbol of the generated constructor, or absent.

  Plain variables are not listed. `__instvar_names__` keeps every name in source order, as today.
- **D-b, opcodes.** `STORE_CONST_INSTVAR = 63`, `STORE_ONCE_INSTVAR = 64` and `PUSH_CONST_INSTVAR = 65`. Each takes the constant-pool symbol index of the variable name, as `STORE_INSTVAR` does.
  - `STORE_CONST_INSTVAR` pops its value like `STORE_INSTVAR`, so the compiler emits `DUP` before it unless the value is discarded.
  - `STORE_ONCE_INSTVAR` **replaces** the top of the stack with the value the variable holds afterwards (§6.3), so the compiler never emits `DUP` before it, and emits `POP` after it when the value is discarded.
  - There is no `PUSH_OWN_CONST_INSTVAR` twin unless the §14.1 gate fails (Task 12).
- **D-c, "unset".** Unset means the own attribute is absent (`getOwnAttributeDirect` answers `nullptr`) or holds nil. A CAS "from unset" tries `setAttributeIfEqual(key, nullptr, v)` and then `setAttributeIfEqual(key, PROTO_NONE, v)`, as `prim_Atom_cas` does (S39).
- **D-d, the generated constructor** is real bytecode, built by hand in C++ (`installConstantConstructor`) when the declaration runs. It is a class-side method with the keyword selector over all constants, inherited first, whose body is:

  ```
  PUSH_SELF; PUSH_LOCAL 0..n-1; MAKE_ARRAY n; PUSH_CONST #sel;
  SEND_KEYWORD #__constructConstants:selector:; RETURN_TOP
  ```

  It is installed on the metaclass directly, not through `__installClassMethod:as:`. The primitive `__constructConstants:selector:` takes the names from the selector's keywords and works as follows:
  1. `newInstance`;
  2. one `setAttributes` with the non-nil arguments;
  3. a send of `initialize`;
  4. the M13 check, naming the selector.

  A primitive alone cannot know its selector (a `ProtoMethod` is a plain function pointer), so a per-class method is required.
- **D-e, the `new` check.** `Object class >> new` (`lib/kernel/object.st`) becomes:

  ```
  instance := self basicNew. instance initialize.
  self __hasConstants ifTrue: [self __checkConstantsOf: instance after: #new].
  ^ instance
  ```

  A class without constants pays one primitive send, which is one `getAttribute` of `__has_constants__`. Spec §6.2 says "one test of a class attribute"; this is that test plus the primitive send. It is measured in Task 12 with `benchmarks/micro/instances.st`.
- **D-f, the M7 depth.** The thread-local `int` that counts `update:with:` block depth lives in `src/primitives/state_prims.cpp`, behind `bool insideUpdateBlock()`. An RAII guard decrements it on every exit, a C++ exception (non-local return, signal, `FutureYield`) included. `prim_Future_wait` tests it first and signals M7 unconditionally, whether the Future is pending or not. `waitWithin:` reaches it through `wait`.
- **D-g, a constant read is not inert.** `Compiler::isInertGroupValue` answers false for a constant (its read can signal). `isInstVarAssignment` answers false for any non-plain variable. So constant and write-once stores, and constant reads after the first value, end a run (§6.6).
- **D-h, private copy primitives.** `deepCopy` and `inspect` use two private primitives:
  - `__rawInstVarNamed:` answers the attribute, or nil when unset, never checking;
  - `__copyWithInstVarValues:` answers a `shallowCopy` whose declared variables (`self class allInstVarNames` order) are replaced in **one** `setAttributes` with the non-nil values given (§11).

  `inspect` prints `<unset constant>` for an unset constant. The spec does not cover `inspect`, so this ruling applies the debugger's row of §6.2.
- **D-i, W1.** A `static std::atomic<bool>` in `state_prims.cpp`, exchanged once. The warning prints once per process, across runtimes too.
- **D-j, DAP.** The DAP server shows frame locals only, and has no instance-variable panel and no `setVariable` request (`src/dap/DapServer.cpp`). So §11's DAP row needs no code in 0.17.0. Record it in STATUS as "not applicable until the DAP shows instance variables".

---

### Task 1: Build environment and baselines

**Files:** none in the repository. Everything goes in `$S`, plus the sibling `/home/gamarino/Documentos/proyectos/worktrees/protoIO`.

**Interfaces:**
- Produces:
  - `$S/pc-361`, a protoCore v3.6.1 Release prefix;
  - `$S/pc-361-tsan`, a ThreadSanitizer prefix;
  - the sibling `../protoIO` at v0.7.1;
  - `$WT/build_release`, the build every later task uses;
  - `$S/ref-0161/build_release/protost`, the 0.16.1 reference for fail-first runs and Task 12;
  - `$S/baseline-ctest.log`.

- [ ] **Step 1: Extract the pinned sources.** Use `git archive` only, so no repository changes:
  - `git -C /home/gamarino/Documentos/proyectos/protoCore archive v3.6.1 | tar -x -C $S/pc-361-src`;
  - the same for protoIO `v0.7.1` into `/home/gamarino/Documentos/proyectos/worktrees/protoIO`, the sibling `../protoIO` that CMakeLists.txt and the TSan build read;
  - protoST `v0.16.1` into `$S/ref-0161`, with its own `../protoIO`: extract protoIO again into `$S/protoIO`.

  Check first that each target directory does not exist. If one does, use a new name and record it.
- [ ] **Step 2: Build and install protoCore 3.6.1, Release.** Configure with `-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$S/pc-361`, then build the targets `protoCore protoCoreConformance protocore-conformance-isolate` and install. Build the TSan prefix `$S/pc-361-tsan` exactly as the `tsan` job in `.github/workflows/ci.yml`.
- [ ] **Step 3: Configure and build protoST.**

  ```bash
  cmake -S $WT -B $WT/build_release -DCMAKE_BUILD_TYPE=Release \
    -DPROTO_CORE_PREFIX=$S/pc-361 -DPROTOCORE_REQUIRE_PACKAGE=ON
  cmake --build $WT/build_release -j"$(nproc)"
  ldd $WT/build_release/protost | grep libprotoCore
  ```

  Expected: `libprotoCore.so.5 => $S/pc-361/lib/...`. Never `/usr`, never `/usr/local`. Build `$S/ref-0161` the same way, into its own `build_release`.
- [ ] **Step 4: Baseline suite.** Run `ctest --test-dir $WT/build_release --timeout 600 --output-on-failure < /dev/null > $S/baseline-ctest.log 2>&1`, sequential. Expected: every test passes; record the count (0.16.1 had 1496). A pre-existing failure is reported to the controller before Task 2 starts, and is not fixed here.
- [ ] **Step 5: Commit.** There is nothing to commit. Record the prefixes, the commit hashes and the count in `$S/env.txt`.

---

### Task 2: Modifier clauses, records and declaration-time checks

**Files:**
- Modify:
  - `src/frontend/AST.h` (ClassDecl gains `ivarModifiers`);
  - `src/frontend/Parser.cpp:642-722` (`parseClassDecl`);
  - `src/frontend/Compiler.h` (`ClassInfo`);
  - `src/frontend/Compiler.cpp` (`collectClasses`, `emitSetInstVarNames`);
  - `src/primitives/object_prims.cpp` (`declareInstVarNames`, `makeSubclassWithMixins`, `prim_Object_addBehavior`, the new primitives, their binds);
  - `lib/kernel/reflection.st`;
  - `CMakeLists.txt` (new sources).
- Create: `src/runtime/IvarModifiers.h`, `src/runtime/IvarModifiers.cpp`.
- Test:
  - `tests/unit/test_ivar_modifiers.cpp` (register it in `tests/CMakeLists.txt`);
  - `tests/conformance/16-explicit-state/modifier-conflict-mixins.st`, `modifier-redeclaration-refused.st`, `modifier-instvarnames-order.st`, `modifier-message-form.st` (spec tests 78–81);
  - `instVarModifierOf.st`.

**Interfaces:**
- Produces, in `src/runtime/IvarModifiers.h` (namespace `protoST`), consumed by every later task:

  ```cpp
  enum class IvarModifier : uint8_t { Plain, Constant, WriteOnce, Atomic };
  const char* modifierName(IvarModifier);              // "plain" "constant" "writeOnce" "atomic"
  bool parseModifierName(std::string_view, IvarModifier&);
  // Runtime lookups; `obj` is a class or an instance (its class chain is walked:
  // the class, its superclasses and mixins, as forClassAndAncestors does).
  // `declared` is set to whether the name is a declared instance variable.
  IvarModifier modifierOf(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* obj,
                          const std::string& name, bool* declared);
  bool declaresInstVar(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* obj,
                       const std::string& name);   // class objects: their class-instance variables
  std::vector<std::string> constantNamesOf(STRuntime&, proto::ProtoContext*,
                                           const proto::ProtoObject* cls);  // inherited first, source order
  std::string constructorSelectorFor(const std::vector<std::string>& constants); // "number:owner:"
  ```

  Move `addDeclaredNames` and `forClassAndAncestors` from the anonymous namespace of `object_prims.cpp` into this unit (declared in the header), and use them from both files.
- Produces:
  - `ast::Node::ivarModifiers`, a `std::vector<IvarModifier>` aligned with the instance-variable names in `stringList[1..1+intValue]`;
  - `Compiler::ClassInfo::modifiers`, a `std::unordered_map<std::string, IvarModifier>` of the class's own non-plain variables.

  Inherited modifiers are resolved by `Compiler::modifierThroughChain(const std::string& cls, const std::string& name) const -> IvarModifier`, which walks the superclass chain and mixins as `instVarsThroughChain` does.
- Produces the runtime selectors:
  - `Behavior >> __setInstVarNames: names modifiers: kindsString`, the encoding of D-a;
  - `Behavior >> subclass:instanceVariableNames:constantNames:writeOnceNames:atomicNames:`;
  - `Behavior >> instVarModifierOf:`, which answers `#plain`, `#constant`, `#writeOnce` or `#atomic`, and signals `Error` with `an Account has no instance variable 'zork'` for an undeclared name.
- Extends `declareInstVarNames(rt, ctx, cls, names, modifiers)`. An empty `modifiers` means all plain. It writes D-a's three attributes; `__constant_constructor__` is written by Task 6.

- [ ] **Step 1: Write the failing tests.**
  - **Parser (unit).** `Object subclass: #Account instanceVariableNames: 'history' constantNames: 'number owner' writeOnceNames: 'oid' atomicNames: 'balance'.` gives `stringList == {"Object","history","number","owner","oid","balance"}`, `intValue == 5`, and `ivarModifiers == {Plain, Constant, Constant, WriteOnce, Atomic}`. Clauses in any order keep source order.
  - **Duplicates (unit).** A name in two clauses compiles to the error `instance variable 'oid' is declared twice in Account`.
  - **Conformance 80, `modifier-instvarnames-order.st`.** With the order `constantNames: 'number owner' instanceVariableNames: 'history' atomicNames: 'balance'`, `Account instVarNames printString` gives `EXPECT: #('number' 'owner' 'history' 'balance')`.
  - **Conformance 81, `modifier-message-form.st`.** `Object subclass: #Acc instanceVariableNames: 'h' constantNames: 'n' writeOnceNames: '' atomicNames: 'b'` (message form), then `{Acc instVarModifierOf: #n. Acc instVarModifierOf: #b. Acc instVarModifierOf: #h}` gives `EXPECT: #(#constant #atomic #plain)`.
  - **Conformance 78, `modifier-conflict-mixins.st`.**
    - `Keyed` declares `constantNames: 'id'` and `Shared` declares `atomicNames: 'id'`.
    - `Object subclass: #Both uses: {Keyed. Shared}.` gives `EXPECT-ERROR: instance variable 'id' is constant in Keyed and atomic in Shared`.
    - Add a second file, `modifier-conflict-addbehavior.st`: the same conflict through `addBehavior:`.
  - **Conformance 79, `modifier-redeclaration-refused.st`.** Two declarations of `Account` in one file: first `atomicNames: 'balance'`, then `instanceVariableNames: 'balance'`. Expected: `EXPECT-ERROR: Account is already declared with 'balance' atomic; a class keeps the modifiers it was declared with`. Also cover a redeclaration that keeps the same modifiers: it succeeds.
  - **`instVarModifierOf.st`.** Inherited modifiers are answered on a subclass. An undeclared name signals the M8-shaped text.
  - **Review focus 4 (unit, `test_ivar_modifiers.cpp`).** Compile a module declaring `Account` with `atomicNames: 'balance'` in REPL mode. In a second compiler with `setKnownClasses(first.classes())`, compile `Account >> f  balance := 1`. Expected: the second compile reports M4 (it fails until Task 3; mark it `[!mayfail]`, and Task 3 removes the tag). Its first half, that `ClassInfo::modifiers` survives `sharedClassRegistrySnapshot()`, passes in this task.
- [ ] **Step 2: Run them; expect failures.** Run `cmake --build $WT/build_release -j"$(nproc)" && ctest --test-dir $WT/build_release -R "16-explicit-state|ivar_modifiers"`. Expected: FAIL (`unknown keyword in class declaration: constantNames:`, and the unknown selectors).
- [ ] **Step 3: Implement.**
  - **Parser.** `parseClassDecl` takes `constantNames:`, `writeOnceNames:` and `atomicNames:` as string-list clauses, appending to `ivs` in source order with the matching modifier.
  - **Compiler.** `collectClasses` fills `ClassInfo::modifiers`. `emitSetInstVarNames` emits `__setInstVarNames:modifiers:` instead of `__setInstVarNames:` **only** when a modifier is present, so every existing listing (VM.md examples 04 and 16) is unchanged. The `uses:` path passes the same through `subclass:instanceVariableNames:uses:`, followed by the modifiers send.
  - **`declareInstVarNames`** additionally signals:
    - M11, when the class already records `__instvar_names__` containing a name whose recorded modifier differs from the new one (a plain redeclaration of a modified name included);
    - M10, by walking each parent's `__ivar_modifiers__`. `makeSubclassWithMixins` and `addBehavior:` run the same M10 check after the chain is built.

  Modifiers apply to instance variables only: a class-instance-variable declaration (`Foo class instanceVariableNames:`) takes none (ruling G1).
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command. Expected: PASS. Then run the whole suite sequentially and compare the count with `$S/baseline-ctest.log` plus the new cases.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): constantNames:, writeOnceNames:, atomicNames: clauses and their class records"`

---

### Task 3: Assignment rules and `STORE_CONST_INSTVAR` (M1–M4)

**Files:**
- Modify:
  - `src/runtime/Opcodes.h`;
  - `src/runtime/ExecutionEngine.{h,cpp}` (label table, `check[]`, handler, `storeConstInstVar`);
  - `src/runtime/Disassembler.cpp`;
  - `src/runtime/BytecodeModule.{h,cpp}` (operand scans; the `isDeoptCopy` flag);
  - `src/frontend/Compiler.{h,cpp}` (both Assignment emitters, `isInstVarAssignment`, `compileDeoptBlock`);
  - `src/primitives/object_prims.cpp` (`collectLateSites`, `resolveLateVariables`, which now needs the selector).
- Test:
  - `tests/unit/test_ivar_modifiers.cpp`, with emission cases;
  - conformance `const-outside-initialize-is-error.st` (49), `const-in-closure-is-error.st` (50), `const-second-initialize-refused.st` (51), `atomic-assign-is-error.st` (68), `modifiers-inherited.st` (77), `late-method-modifier-rules.st`, and `const-in-deopt-copy.st`.

**Interfaces:**
- Consumes: `modifierThroughChain`, `modifierOf` (Task 2).
- Produces:
  - `Op::STORE_CONST_INSTVAR = 63`;
  - `Op::STORE_ONCE_INSTVAR = 64`, reserved here and emitted by Task 4;
  - `BytecodeModule::setDeoptCopy(bool)` and `isDeoptCopy() const`;
  - `ExecutionEngine::storeConstInstVar(proto::ProtoContext*, const Frame&, uint32_t nameConst, const proto::ProtoObject* val)`;
  - `resolveLateVariables(rt, ctx, cls, classSide, methodObj, const std::string& selector)`, whose callers pass the selector they install.

Rules (spec §6.2, §6.4, §6.5):
- **Constant.** `:=` compiles to `STORE_CONST_INSTVAR` only in an instance-side `initialize`: in its body, in the blocks the compiler inlines there, and in the de-optimised copies of those blocks.
  - Inside a real closure it is M2: `cannot assign to the constant 'number' of Account inside a block: the block may run after construction (line N)`.
  - Anywhere else (other selectors, class-side `initialize`) it is M1: `cannot assign to the constant 'number' of Account outside initialize (line N)`.
- **Atomic.** `:=` is always M4: `cannot assign to the atomic variable 'balance' of Account: use update:with: or compareAndSet:from:to: (line N)`. The class named is the method's class.
- **Late install.** At install time, `PUSH_LATE_VAR` and `STORE_LATE_VAR` resolve by the class's modifiers:
  - a constant store becomes `STORE_CONST_INSTVAR`, or M1/M2;
  - a write-once store becomes `STORE_ONCE_INSTVAR`;
  - an atomic store is M4;
  - a constant read becomes `PUSH_CONST_INSTVAR` (that opcode is emitted from Task 5; until then it resolves to `PUSH_INSTVAR`).

  A site is "inside a real closure" when the sub-module chain from the method to the site crosses a module with `!isDeoptCopy()`. `collectLateSites` carries that flag down.
- **Run time of `STORE_CONST_INSTVAR`.**
  - With `val` nil: when the constant is unset, nothing happens; otherwise M3.
  - Otherwise a CAS from unset (D-c); losing it is M3, `ModificationForbidden`: `the constant 'number' of an Account is already set; nothing was written`.
  - An immutable receiver also signals `ModificationForbidden`.

  Use `signalErrorNamed(rt, ctx, "ModificationForbidden", text)` through a `ClassedErrorSignal` thrown from the helper, as `storeInstVar` throws today.

- [ ] **Step 1: Write the failing tests.**
  - **Unit (emission).**
    - `Account >> initialize  number := 7` disassembles to `STORE_CONST_INSTVAR`, and never to `STORE_INSTVAR` or `IVAR_GROUP*`.
    - `initialize  history := 1. number := 2. history := 3` makes no group that contains `number`.
    - `initialize  x > 0 ifTrue: [number := 1]` compiles. Its deopt copy contains `STORE_CONST_INSTVAR`, and the sub-module answers `isDeoptCopy()`.
  - **49.** `Account >> setNumber: n  number := n` gives `EXPECT-ERROR: cannot assign to the constant 'number' of Account outside initialize`.
  - **50.** `initialize  [number := 1] value` gives `EXPECT-ERROR: inside a block: the block may run after construction`. The same file's twin `const-in-inlined-block-allowed.st` uses `initialize  true ifTrue: [number := 1]` and gives `EXPECT: 1` when it reads `number`.
  - **51.** `a := Account new` (its `initialize` sets `number := 7`), then `[a initialize] on: ModificationForbidden do: [:e | e messageText]` gives `EXPECT: the constant 'number' of an Account is already set; nothing was written`.
  - **68.** `Account >> reset  balance := 0` gives `EXPECT-ERROR: cannot assign to the atomic variable 'balance' of Account: use update:with: or compareAndSet:from:to:`.
  - **77.** A subclass method `Sub >> f  balance := 1` on an inherited atomic variable gives the same M4 text, naming `Sub`.
  - **Review focus 1, `late-method-modifier-rules.st`.** A class built by the message form of Task 2 is compiled late:
    - `Acc >> setN: x  n := x` is refused **at install** with the M1 text;
    - `Acc >> b: x  b := x` is refused at install with the M4 text;
    - `Acc >> initialize  n := 5` installs, and `Acc new n` answers `5` once Task 5 lands; for now the test reads it through `instVarNamed:`.
  - **Review focus 2, `const-in-deopt-copy.st`.** `initialize  flag ifTrue: [number := 1]`, where `flag` holds a non-Boolean whose `ifTrue:` is defined to evaluate the block, so the de-optimised copy runs. Expected: `number` is set (EXPECT: 1), with no compile or install error.
  - Remove the `[!mayfail]` tag from Task 2's review-focus-4 unit test.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R "16-explicit-state|ivar_modifiers"`. Expected: FAIL (plain stores are accepted).
- [ ] **Step 3: Implement.** Add the opcodes, the engine handler (`DISPATCH_DIRECT`, with the pinning inside the helper), the Disassembler names and operand decoding, the BytecodeModule operand-scan cases (operand = constant symbol, as `STORE_INSTVAR`), the compiler rules, `isInstVarAssignment` (false for any non-plain), and the install-time resolution.
  - In-closure detection at compile time: some scope between the method scope and the current one is a block scope with `!deoptBoundary`.
  - Errors found while compiling a deopt copy are dropped as today, but its emitted opcode must be `STORE_CONST_INSTVAR`.
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite sequentially. Expected: PASS, and `docs/vm-examples` unchanged.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): constant and atomic assignment rules, STORE_CONST_INSTVAR"`

---

### Task 4: Write-once stores, `STORE_ONCE_INSTVAR` (M12)

**Files:**
- Modify: `src/runtime/ExecutionEngine.{h,cpp}`, `src/runtime/Disassembler.cpp`, `src/runtime/BytecodeModule.cpp`, `src/frontend/Compiler.cpp` (both Assignment emitters).
- Test:
  - unit emission;
  - conformance `once-equal-loss-silent.st` (61), `once-unequal-refused.st` (62), `once-equality-is-the-installed-values.st` (63), `once-nil-never-replaces.st` (64);
  - `once-assignment-value.st`.

**Interfaces:**
- Consumes: `Op::STORE_ONCE_INSTVAR` (Task 3); `sendDynamic` (`object_prims.cpp`); `loopConditionIsTrue` (`NativeExceptionBridge.h`).
- Produces: `const proto::ProtoObject* ExecutionEngine::storeOnceInstVar(proto::ProtoContext*, const Frame&, uint32_t nameConst, const proto::ProtoObject* val)`, which returns the value the variable holds afterwards.

Semantics (§6.3), in order:
1. `val` nil: answer the current value (nil when unset). Never signal.
2. A CAS from unset (D-c) that succeeds answers `val`.
3. Otherwise read the installed `w`. When `w == val`, answer `w`.
4. Otherwise send `=` to `w` with `val`, through `sendDynamic`, with `w` pinned. The truth is `loopConditionIsTrue(rt, ctx, answer, "=")`, so a non-Boolean signals `NonBooleanReceiver`.
5. True answers `w`. False signals M12, `ModificationForbidden`: `the write-once variable 'key' of a Holder already holds a different value; nothing was written`.

An exception from `=` propagates. The handler ends with `DISPATCH_SCOPED()` when a pin lives in its scope; prefer keeping all pins inside the helper and using `DISPATCH_DIRECT()`.

- [ ] **Step 1: Write the failing tests.** In each, `Holder` declares `writeOnceNames: 'key'`, with `Holder >> setKey: x  ^ key := x`.
  - **Unit.** `setKey:` compiles to `STORE_ONCE_INSTVAR` followed by `RETURN_TOP`, with no `DUP`. As a statement whose value is discarded, it is `STORE_ONCE_INSTVAR POP`.
  - **61.** Spec item 61 verbatim:
    - `k1 := 'alpha-' , 'key'`, `k2 := 'alpha' , '-key'`;
    - check `(k1 == k2) not` first;
    - `{ (o setKey: k1) == k1. (o setKey: k2) == k1 }` gives `EXPECT: #(true true)`.
  - **62.** `o setKey: 'alpha-key'`, then `[o setKey: 'beta-key'] on: ModificationForbidden do: [:e | e messageText]` gives `EXPECT: the write-once variable 'key' of a Holder already holds a different value; nothing was written`. A further line checks that `key` is still `'alpha-key'`.
  - **63.**
    - With a class `AllEqual` whose `=` answers true, a second unequal write is silent and answers the first value.
    - With a class `BadEq` whose `=` signals `Error signal: 'eq'`, the second write propagates `eq`, and the variable keeps the first value.
    - With `=` answering `3`, the write signals `NonBooleanReceiver`.
  - **64.** `o setKey: nil` on an unset `key` answers nil and leaves it unset. After `o setKey: 'v'`, `o setKey: nil` answers `'v'` and signals nothing.
  - **`once-assignment-value.st`.** `^ key := aSpec` answers the installed value when the write loses silently (§6.3, the `_publishKeySpec:` form).
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R "16-explicit-state/once|ivar_modifiers"`. Expected: FAIL.
- [ ] **Step 3: Implement** the opcode, the compiler emission (write-once variables, no `DUP`, `POP` when discarded), the Disassembler and the operand scans. Install-time resolution for write-once stores was added in Task 3.
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): write-once stores with the installed value's equality (M12)"`

---

### Task 5: Checked constant reads, `UnsetConstantError` (M14)

**Files:**
- Modify:
  - `src/runtime/Opcodes.h`, `ExecutionEngine.cpp`, `Disassembler.cpp`, `BytecodeModule.cpp`;
  - `src/frontend/Compiler.cpp` (Identifier reads; `isInertGroupValue`, D-g);
  - `src/primitives/object_prims.cpp` (`prim_Object_instVarNamed`; late resolution of constant reads);
  - `src/primitives/exception_prims.cpp` and `src/runtime/NativeExceptionBridge.h` (`signalUnsetConstantError`);
  - `lib/kernel/exception.st`.
- Test:
  - conformance `const-loader-forgets-one.st` (88), `const-pharo-new-override.st` (89), `const-read-before-assignment-in-initialize.st` (90), `const-instVarAt-unset.st` (91);
  - `unset-constant-error-class.st` (94), `unset-constant-error-precise.st` (95), `unset-constant-error-accessors.st` (96), `unset-constant-error-every-path.st` (97);
  - `const-read-in-write-group.st`;
  - a unit case in `tests/unit/test_instvar_groups.cpp`.

**Interfaces:**
- Produces:
  - `Op::PUSH_CONST_INSTVAR = 65`;
  - in `NativeExceptionBridge.h`: `const proto::ProtoObject* signalUnsetConstantError(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* receiver, const std::string& variableName)`;
  - the kernel class `Error subclass: #UnsetConstantError instanceVariableNames: 'receiver variableName'`, with `receiver` and `variableName` readers, in `lib/kernel/exception.st` beside `ModificationForbidden`.
- The signal helper builds the instance, sets `receiver` (the object) and `variableName` (a String), sets `messageText` to `constant '<name>' of <a|an> <ClassName> read before it was set`, and signals it. Build it as `signalMessageNotUnderstood` builds its exception with a receiver.
  - The article comes from the same vowel rule as the default `printString` (`object_prims.cpp`, around line 1933). Extract that rule into a shared helper `std::string articleAndClassName(STRuntime&, ProtoContext*, const ProtoObject*)`.
  - It never sends `printOn:`.
- The handler:
  - reads `self->getAttribute(ctx, sym)`, as `PUSH_INSTVAR` does;
  - pushes a value that is neither `nullptr` nor nil;
  - otherwise pushes the result of `signalUnsetConstantError`, as `ASSERT_LOOP_BOOL` pushes `loopConditionIsTrue`'s result.

- [ ] **Step 1: Write the failing tests.** `Account` declares `constantNames: 'number owner'`, with readers `number` and `owner`.
  - **88.** `a := Account basicNew. a instVarNamed: 'number' put: 7.`, then `{a number. [a owner] on: UnsetConstantError do: [:e | e messageText]}` gives `EXPECT: #(7 'constant ''owner'' of an Account read before it was set')`.
  - **89.** `Account class >> new  ^ self basicNew initialize` (its `initialize` sets only `number`). `Account new` answers without M13, and the first `owner` signals M14.
  - **90.** `initialize  number isNil ifTrue: [number := 1]` signals M14 at the read.
  - **91.** `instVarAt: 2` and `instVarNamed: 'owner'` on an unset `owner` signal M14. On a set constant they answer it.
  - **94.** `{UnsetConstantError superclass == Error. ([Account basicNew owner] on: UnsetConstantError do: [:e | e class]) == UnsetConstantError. ([Account basicNew owner] on: UnsetConstantError do: [:e | e isResumable])}` gives `EXPECT: #(true true false)`.
  - **95.** Spec item 95: inner `on: UnsetConstantError` inside outer `on: Error`, with four protected blocks. Only the unset read reaches the inner handler; print the handler that caught each.
  - **96.** Spec item 96 exactly, including a `printOn:` override that reads `owner`. The text is unchanged and nothing recurses.
  - **97.** The compiled read, `instVarAt:` and `instVarNamed:` give equal `receiver`, `variableName` and `messageText`.
  - **Review focus 5, `const-read-in-write-group.st`.** `Account >> f  history := 1. history2 := owner. ^ self` on a `basicNew` instance, under `[...] on: UnsetConstantError do: [:e | e receiver instVarNamed: 'history']`, gives `EXPECT: 1`. The read ends the run, so `history` was published. Add the unit case to `test_instvar_groups.cpp`: no group spans a constant read.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R "16-explicit-state|instvar_groups"`. Expected: FAIL (reads answer nil; `UnsetConstantError` is undefined).
- [ ] **Step 3: Implement.**
  - The compiler emits `PUSH_CONST_INSTVAR` for a constant read, in methods and blocks alike. The late path does the same.
  - `prim_Object_instVarNamed` checks constants through `modifierOf`.
  - `instVarAt:` reaches it through `lib/kernel/reflection.st`, unchanged.
  - The debugger does not check reads (§6.2).
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): checked constant reads and UnsetConstantError (M14)"`

---

### Task 6: Generated constructor, end-of-construction check (M9, M13)

**Files:**
- Create: `src/runtime/ConstantConstructor.{h,cpp}`.
- Modify:
  - `src/primitives/object_prims.cpp` (`declareInstVarNames` installs the constructor; `prim_Object_installClassMethod` refuses M9; the new primitives);
  - `src/frontend/Compiler.cpp` (compile-time M9 for a visible class);
  - `lib/kernel/object.st` (`Object class >> new`, D-e);
  - `CMakeLists.txt`.
- Test: conformance `const-by-generated-constructor.st` (47), `const-in-initialize.st` (48), `const-nil-when-unset.st` (52), `const-unset-after-new.st` (55), `const-nil-from-constructor.st` (56), `const-inherited-unset.st` (57), `const-new-override-checked.st` (58), `const-basicNew-unchecked.st` (59), `const-check-no-constants.st` (60), `const-constructor-redefinition-refused.st`, `const-inherited-constructor-on-subclass.st`.

**Interfaces:**
- Consumes: `constantNamesOf`, `constructorSelectorFor` (Task 2); `newInstance` (`InstanceTemplate.h`).
- Produces:
  - `void installConstantConstructor(STRuntime&, proto::ProtoContext*, const proto::ProtoObject* cls)`, which builds the D-d module, adopts it (`rt.adoptModule`), wraps it the way `PUSH_BLOCK` + `__installClassMethod:as:` do, sets it on `ensureMetaclass(cls)` under the selector, and records `__constant_constructor__`;
  - `Behavior >> __constructConstants: anArray selector: aSymbol`;
  - `Behavior >> __hasConstants`;
  - `Behavior >> __checkConstantsOf: anInstance after: aSymbol`, which signals M13 (`Error`) for the first unset constant in `constantNamesOf` order: `the constant 'owner' of an Account is not set at the end of construction (number:owner:)`.

  `declareInstVarNames` calls `installConstantConstructor` for every class that declares or inherits a constant. A subclass that adds no constant gets the same selector.
- **M9** (`number:owner: is the generated constructor of Account and cannot be redefined; give your constructor another name`) is signalled:
  - at compile time, for a class-side MethodDecl whose selector equals `constructorSelectorFor(chain constants)`;
  - at install time, by `__installClassMethod:as:` when the selector equals the class's `__constant_constructor__`.

- [ ] **Step 1: Write the failing tests.** `Account` declares `instanceVariableNames: 'seen'` and `constantNames: 'number owner'`, with `initialize  seen := number`.
  - **47.** `{(Account number: 7 owner: 'ada') number. (Account number: 7 owner: 'ada') seen}` gives `EXPECT: #(7 7)`.
  - **48.** `new` plus an `initialize` that sets both constants answers them.
  - **52.** `initialize  number := nil. number := 3. owner := 'x'` gives `EXPECT: 3`. In a second program, assigning nil leaves the constant unset, and `new` then signals M13 for it.
  - **55.** `[Account new] on: Error do: [:e | e messageText]`, with an `initialize` that sets only `number`, gives `EXPECT: the constant 'owner' of an Account is not set at the end of construction (new)`.
  - **56.** `Account number: 7 owner: nil` gives `... (number:owner:)`.
  - **57.** A subclass `initialize` without `super initialize` leaves the inherited `number` unset and gives M13 naming `number`.
  - **58.** `Account class >> new  ^ super new` is checked (M13).
  - **59.** `Account basicNew` signals nothing. `instVarNamed:put:` then fills both, and the readers answer them.
  - **60.** A class without constants: `new` runs `initialize`, which counts in a class variable, and nothing else changes (`EXPECT: 1`).
  - **`const-constructor-redefinition-refused.st`.** `Account class >> number: n owner: o ^ nil` gives `EXPECT-ERROR: number:owner: is the generated constructor of Account and cannot be redefined`. A twin file covers the late (install-time) path with a message-form class.
  - **`const-inherited-constructor-on-subclass.st`.** `Sub` adds `constantNames: 'extra'`. Then `Sub number: 1 owner: 'a'` gives M13 naming `extra` and `(number:owner:)`, and `Sub number: 1 owner: 'a' extra: 2` answers a `Sub`.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R 16-explicit-state/const`. Expected: FAIL.
- [ ] **Step 3: Implement** as in the Interfaces.
  - The constructor's single `setAttributes` happens before `initialize`, so `initialize` reads the constants freely.
  - The constructor never writes nil (§6.2).
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS. Then run `benchmarks/micro/instances.st` once to check it still prints `instances ok`. Its timing is measured in Task 12, not here.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): generated constant constructor and end-of-construction check (M9, M13)"`

---

### Task 7: `compareAndSet:from:to:`, `update:with:`, M5/M7/M8, `setInstVar:from:to:` alias with W1

**Files:**
- Create: `src/primitives/state_prims.cpp` (`installStatePrimitives(STRuntime&)`, called beside `installAtomPrimitives`).
- Modify:
  - `src/primitives/object_prims.cpp` (remove `prim_Object_setInstVarFromTo` and its bind);
  - `src/primitives/future_prims.cpp` (`prim_Future_wait`, M7 first);
  - `include/protoST/primitives.h` or the existing installer list;
  - `CMakeLists.txt`.
- Migrate `tests/conformance/10-actors/atom-new-cas-from-nil.st` and `setinstvar-cas.st` to `compareAndSet:from:to:`. Keep their meaning.
- Test:
  - conformance `const-cas-refused.st` (54), `once-cas-adopts.st` (65), `once-update-refused.st` (66), `atomic-update-answers-installed.st` (69), `atomic-compare-and-set.st` (70), `atomic-update-nonlocal-return-abandons.st` (71), `atomic-update-exception-abandons.st` (72), `atomic-wait-in-update-is-error.st` (73), `cas-undeclared-name.st` (75), `plain-update-allowed.st` (76), `setinstvar-alias.st` (85), `update-depth-restored.st`;
  - `tests/cli/test_cli_setinstvar_deprecation.sh`, registered as `cli_setinstvar_deprecation` in `CMakeLists.txt`.

**Interfaces:**
- Consumes: `modifierOf`, `declaresInstVar` (Task 2); `invokeBlock` (`block_prims.cpp`); D-c.
- Produces:
  - `Object >> compareAndSet: aName from: expected to: new` (Boolean);
  - `Object >> update: aName with: aBlock` (the installed value);
  - `Object >> setInstVar:from:to:` (the alias: W1 once, then the same code);
  - `bool insideUpdateBlock()` (D-f), declared in `src/primitives/state_prims.h`.
- Checks, before anything is read or written:
  - The name must be a `String` or `Symbol` and declared, or M8, `Error`: `an Account has no instance variable 'zork'`. The text uses `articleAndClassName`.
  - Then the modifier, per §6.5:
    - constant → M5 for both messages;
    - write-once → `compareAndSet:` only `from: nil`, else M5; `update:with:` → M5;
    - atomic and plain → allowed.
  - M5 text: `<selector> cannot write the <modifier phrase> '<name>' of <a|an> <Class>; <advice>`. The phrases are `constant` and `write-once variable`. The advice is `assign it in initialize` for a constant and `assign it with :=` for a write-once variable.
  - An immutable receiver signals `ModificationForbidden`, never false.
  - Ruling G2: `compareAndSet: #w from: nil to: nil` on a write-once variable writes nothing and answers whether the variable is unset.
- `update:with:` is the `prim_Atom_swap` loop over `_iv_<name>`, with the old and new values pinned and the depth guard around `invokeBlock`.

- [ ] **Step 1: Write the failing tests.** Use spec items 54, 65, 66 and 69–76 and 85 as written. Exact expectations:
  - **65.** `{o compareAndSet: #oid from: nil to: 1. o compareAndSet: #oid from: nil to: 2. o oid}` gives `EXPECT: #(true false 1)`.
  - **70.** `{a compareAndSet: #balance from: nil to: 0. a compareAndSet: #balance from: 5 to: 1}` gives `EXPECT: #(true false)`.
  - **71 and 72.** After the abandoned update, `balance` still holds its earlier value.
  - **73.**
    - `[a update: #balance with: [:old | Future new wait]] on: Error do: [:e | e messageText]` gives `EXPECT: wait inside an update:with: block: the block must be pure and may run several times`.
    - A second case uses `(Future new resolve: 1; yourself) wait`: an already-settled Future still signals M7.
  - **75.** M8 for both messages, and for the alias.
  - **85.** `setInstVar:from:to:` behaves as `compareAndSet:from:to:`, M8 included.
  - **Review focus 3, `update-depth-restored.st`.** After an update abandoned by a non-local return and one abandoned by an exception, `(Future new resolve: 7; yourself) wait` answers `7` (`EXPECT: 7`).
  - **CLI.** `protost -e` runs three `setInstVar:from:to:` calls. The script checks that stderr has the W1 line exactly once (`grep -c` gives 1), with the full pinned text, and that stdout is the expected value.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R "16-explicit-state|cli_setinstvar|10-actors/(atom-new-cas|setinstvar)"`. Expected: the new cases FAIL; the migrated ones fail with MNU.
- [ ] **Step 3: Implement** `state_prims.cpp`, the M7 check, and the alias with W1.
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS. Then `grep -rn "setInstVar:" lib tests examples benchmarks` lists only `setinstvar-alias.st` and the CLI test.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): compareAndSet:from:to:, update:with:, setInstVar:from:to: deprecated (W1)"`

---

### Task 8: Reflective writes, copies, `ModificationForbidden` for refused stores

**Files:**
- Modify:
  - `src/primitives/object_prims.cpp` (`prim_Object_instVarNamedPut`; `__rawInstVarNamed:`, `__copyWithInstVarValues:`);
  - `src/runtime/ExecutionEngine.cpp` (`storeInstVar` and `IVAR_GROUP_END`'s immutable branch throw `ClassedErrorSignal("ModificationForbidden", …)` with the same text);
  - `lib/kernel/reflection.st` (`Object >> deepCopy`, `Object >> inspect`, D-h).
- Test: conformance `const-reflection-fills-unset-only.st` (53), `once-reflection-rules.st` (67), `atomic-reflection-refused.st` (74), `store-immutable-receiver-is-modification-forbidden.st` (82), `deepcopy-with-modifiers.st` (83), `const-loader-fills-by-reflection.st` (87), `const-copy-keeps-unset.st` (93), `inspect-unset-constant.st`.

**Interfaces:**
- Consumes: `modifierOf` (Task 2); D-c; `articleAndClassName` (Task 5).
- Produces: `Object >> __rawInstVarNamed: aName` and `Object >> __copyWithInstVarValues: anArray` (private; D-h).
- `instVarNamed:put:` rules (§11):
  - plain: writes;
  - constant: only while unset, by CAS from unset, else M3; a nil value is a no-op while unset;
  - write-once: only while unset, else M6, even with an equal value: `instVarNamed:put: cannot replace the write-once variable 'oid' of a PBAtom`;
  - atomic: always M6, with the exact §12 text `instVarNamed:put: cannot write the atomic variable 'balance' of an Account: use update:with: or compareAndSet:from:to:`.

  `instVarAt:put:` reaches it through `reflection.st`, unchanged.

- [ ] **Step 1: Write the failing tests.**
  - **82.** Reach `STORE_INSTVAR` on an immutable receiver through a mixin method: `Object subclass: #M instanceVariableNames: 'x'. M >> setX  x := 1.`, then `BlockClosure addBehavior: M`, then `([[] setX] on: ModificationForbidden do: [:e | e class name])`. Expected: `EXPECT: ModificationForbidden`.
    - First confirm on `$S/ref-0161/build_release/protost` that the same program raises the NativeError text `instance variable assignment: the receiver is an immutable value; nothing was written`. If `addBehavior:` cannot reach a block, find another route to `storeInstVar`'s immutable branch and record it in the test's comment.
  - **83.** `deepCopy` of an `Account` with both constants set, a set write-once variable and an atomic variable answers a copy with equal values, and with not identical values for mutable ones.
  - **93.** `shallowCopy` and `deepCopy` of a `basicNew` instance with an unset `owner` succeed; reading `owner` on the copy signals M14.
  - **53, 67, 74, 87.** As in spec §14.2, with the texts above.
  - **`inspect-unset-constant.st`.** `Account basicNew inspect` prints `  owner: <unset constant>` on the Transcript and signals nothing.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R 16-explicit-state`. Expected: FAIL.
- [ ] **Step 3: Implement** as specified.
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS, with `06-blocks/block-instvar-write-is-refused.st` still green.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): reflection honours modifiers; copies move unset constants; refused stores signal ModificationForbidden"`

---

### Task 9: `instVarIsSet:` (M15)

**Files:**
- Modify: `src/primitives/state_prims.cpp`.
- Test: conformance `const-instVarIsSet.st` (92), `instVarIsSet-plain.st` (99), `instVarIsSet-constant.st` (100), `instVarIsSet-write-once.st` (101), `instVarIsSet-atomic.st` (102), `instVarIsSet-unknown-name.st` (104).

**Interfaces:**
- Consumes: `declaresInstVar` (Task 2).
- Produces: `Object >> instVarIsSet: aName`, which answers true or false. It is one `getOwnAttributeDirect` and a nil test. It allocates nothing and never signals for a declared name.
  - On a class object, the declared names are its class-instance variables.
  - A non-name argument gives M15, `instVarIsSet: expects a String or Symbol, not 3`.
  - An undeclared name (including any name on `3`) gives `instVarIsSet: <a|an> <Class> has no instance variable '<name>'`. For `3` the class part is `a SmallInteger`.

- [ ] **Step 1: Write the failing tests** for spec items 92, 99–102 and 104.
  - **104** pins `instVarIsSet: an Account has no instance variable 'ownr'` and `instVarIsSet: expects a String or Symbol, not 3`.
  - **99.** `{o instVarIsSet: #x. (o setX: 1; instVarIsSet: #x). (o setX: nil; instVarIsSet: #x)}` gives `EXPECT: #(false true false)`.
- [ ] **Step 2: Run them; expect failures.** Run `ctest --test-dir $WT/build_release -R 16-explicit-state/instVarIsSet`. Expected: FAIL (MNU).
- [ ] **Step 3: Implement** the primitive.
- [ ] **Step 4: Run the tests; expect them to pass.** Run the same command, then the whole suite. Expected: PASS.
- [ ] **Step 5: Commit.** `git commit -m "feat(explicit-state): instVarIsSet: probe (M15)"`

---

### Task 10: Concurrency tests (T1, T2, T4, T5, test 98) under ThreadSanitizer

**Files:**
- Test (each self-reports its work as a final printed value):
  - `tests/conformance/16-explicit-state/t1-atomic-deposits.st`;
  - `t2-write-once-race-equal.st`, `t2-write-once-race-unequal.st`, `t2-write-once-race-cas.st`;
  - `t4-constants-visible-to-actors.st`;
  - `t5-wait-in-update-on-worker.st`;
  - `unset-constant-error-across-actor.st` (98).

  Each mentions `asActor` or `Future`, so it gets the `tsan` label through the existing regex in `tests/CMakeLists.txt:206-212`. Check with `ctest -N -L tsan | grep 16-explicit-state`.
- Modify: none, unless a test exposes a bug. A bug is fixed in the task that owns the code, with its own commit.

**Interfaces:**
- Consumes: Tasks 4–7.

- [ ] **Step 1: Write the tests.** No sleeps and no timing assertions. Every race outcome is checked by count, not by timing.
  - **T1.** 8 actors each send `deposit: 1` 10,000 times to one shared `Account` (`atomicNames: 'balance'`, `deposit:` uses `update:with:`). The object is passed directly, not as an actor. Wait on all the Futures. Expected: `EXPECT: 80000`.
  - **T2 equal.** 16 actors each build an equal String longer than 6 bytes by concatenation; check pairwise that none is `==` another. Each calls `setKey:`. Expected: every answer `==` the installed value, and no signal: `EXPECT: #(16 0)` as (answers identical to installed, signals).
  - **T2 unequal.** 16 different values through `:=`. Expected: exactly one install and 15 M12 signals, and the variable holds the value whose writer saw no signal: `EXPECT: #(1 15 true)`.
  - **T2 CAS.** 16 candidates through `compareAndSet: #oid from: nil to:`. Expected: exactly one `true`, and every racer then reads the winner: `EXPECT: #(1 16)`.
  - **T4.** Construct with `Account number: i owner: …` and immediately send the object to an actor, which answers `number` and `owner`, 1,000 times. Every answer is correct: `EXPECT: 1000`.
  - **T5.** An actor method runs `update:with:` whose block waits on a Future that another actor would settle. Expected: M7 is signalled and caught by the method, which answers the error text. The program ends without hanging: `EXPECT: wait inside an update:with: block: the block must be pure and may run several times`.
  - **98.** An actor's method reads an unset constant. `[(actor read) wait] on: UnsetConstantError do: [:e | {e variableName. e receiver class name}]` gives `EXPECT: #('owner' 'Account')`.
- [ ] **Step 2: Run them on the 0.16.1 reference to see them fail.** Each fails to parse (`constantNames:`), or fails with MNU.
- [ ] **Step 3: Run them on the release build.** `ctest --test-dir $WT/build_release -R 16-explicit-state/t[0-9]` passes, run three times in a row (a race test must not be flaky).
- [ ] **Step 4: Run under TSan.**
  - Configure `$WT/build_tsan` as the CI `tsan` job does, with `-DPROTO_CORE_PREFIX=$S/pc-361-tsan`. Set `sudo sysctl vm.mmap_rnd_bits=28` first if TSan stops with "unexpected memory mapping"; if sudo is not available, report it.
  - Run `ctest --test-dir $WT/build_tsan -L tsan --timeout 900 --output-on-failure < /dev/null > $S/tsan.log 2>&1`, sequentially.
  - Expected: all pass, including `tsan_reports_check`.
- [ ] **Step 5: Commit.** `git commit -m "test(explicit-state): concurrency tests T1, T2, T4, T5 and an UnsetConstantError across an actor"`

---

### Task 11: Documentation, the VM listing (test 84), STATUS

**Files:**
- Modify:
  - `docs/LANGUAGE.md`:
    - new **§4.13 Instance-variable modifiers**, from spec §6 restricted to 0.17.0: the §6.1 tables, §6.2–§6.6, §6.8 and §7, with examples that state their results;
    - a **locals** note in §4.5 (spec §4, the "why" text);
    - **§8.1**: `UnsetConstantError` in the hierarchy, and `ModificationForbidden`'s line extended to refused instance-variable stores;
    - **§10.8**: `setInstVar:from:to:` deprecated, and `Atom` described as one atomic variable (§6.7);
    - new **§10.9 One pattern from the attribute to the commit**, with spec §9's table (omit the value-class row's `withAll:` wording until 0.18.0, keeping "constructor");
    - the table of contents;
  - `docs/tutorial/10-actors-and-futures.md`: replace `setInstVar:from:to:` with `compareAndSet:from:to:` and add an `update:with:` example;
  - `docs/VM.md`:
    - three rows in section 2.3;
    - the opcode count at the top of section 2 (60 → 63 values) and the "next free value" in section 4;
    - new **Example 20**;
  - `docs/STATUS.md`:
    - an Implemented entry under Object model;
    - deviations **D60** (the modifier clauses and their semantics, the M13 check, the write-once equality rule), **D61** (waits refused inside `update:with:`), **D62** (the constant read check, `UnsetConstantError`, `instVarIsSet:`; Pharo has neither) and **D63** (`setInstVar:from:to:` deprecated in 0.17.0, removed in 0.18.0);
    - the D-j note;
    - the suite count;
  - `docs/CONFORMANCE.md` and `tests/conformance/README.md`: the `16-explicit-state/` folder.
- Create: `docs/vm-examples/20-explicit-state.st` and its golden `20-explicit-state.txt`.

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write the vm-example (test 84).** A class with `instanceVariableNames: 'a b' constantNames: 'k' writeOnceNames: 'w'` and

  ```
  initialize  a := 1. b := 2. k := 3. a := 4. w := 5
  ```

  Its listing must show `IVAR_GROUP`/`IVAR_GROUP_END` for `a`/`b`, then `STORE_CONST_INSTVAR`, the `a` store, `STORE_ONCE_INSTVAR` and `PUSH_CONST_INSTVAR` (a reader `k ^ k`). Add the VM.md marker `<!-- vm-example: 20-explicit-state -->` and the listing. Then run `python3 -I docs/vm-examples/disasm_examples.py $WT/build_release/protost --update`.
- [ ] **Step 2: Run the docs checks before writing the prose.** Run `ctest --test-dir $WT/build_release -R "^docs/"`. Expected: `docs/vm-examples` passes with the new example.
- [ ] **Step 3: Write the prose.** Every LANGUAGE.md and tutorial snippet that states a result uses the checker's `"=> …"` convention (`tests/docs/run_doc_snippets.py`, conventions at the top), so it runs.
- [ ] **Step 4: Run the docs tests; expect them to pass.** Run `ctest --test-dir $WT/build_release -R "^docs/"`. Expected: all `docs/*` pass. Then run the whole suite.
- [ ] **Step 5: Commit.** `git commit -m "docs(explicit-state): LANGUAGE 4.13, 10.9, 8.1; VM example 20; STATUS D60-D63"`

---

### Task 12: The §14.1 performance gate

**Files:**
- Create:
  - `benchmarks/micro/const_read.st`, the exact twin of `benchmarks/comparable/attr_lookup.st` with `constantNames: 'a b c'`, built by `FastObject a: 1 b: 2 c: 3`, which ends with `VERIFIED 600000`;
  - `benchmarks/const_read_gate.py`.
- Modify: `benchmarks/README.md` (the gate and how to run it).
- Report: `benchmarks/reports/2026-11-23-release-0.17.0-const-read.md`, plus raw data in `benchmarks/reports/data/`.

**Interfaces:**
- `const_read_gate.py --old $S/ref-0161/build_release/protost --new $WT/build_release/protost --libdir $S/pc-361/lib --json OUT`. For each (build, program) it:
  1. copies the binary and `libprotoCore.so*` into a **fresh** directory under `$S/gate-runs/<n>`;
  2. runs `perf stat -r 3 -e cycles -x,` with `LD_LIBRARY_PATH` set to that directory;
  3. checks `ldd` resolves the copy;
  4. checks every run's output has `VERIFIED 600000`;
  5. refuses to start while a compiler, `ctest` or another `proto*` process runs (reuse `competitors_running` from `compare_builds.py`).

  It reports the mean cycles, and checks both bounds:
  - attr_lookup(new) ≤ 1.01 × attr_lookup(old);
  - const_read(new) ≤ 1.03 × attr_lookup(new).

  It exits non-zero on a breach.

- [ ] **Step 1: Write the benchmark, and show that it cannot run on 0.16.1.** Run `$S/ref-0161/build_release/protost benchmarks/micro/const_read.st`. Expected: a parse error. Record it as the fourth figure's status (ruling G3). Then run it on the new build. Expected: `VERIFIED 600000`.
- [ ] **Step 2: Run the gate serially on a quiet machine.** Run `python3 benchmarks/const_read_gate.py ...`. Expected: both bounds hold, or the script exits non-zero.
- [ ] **Step 3: If a bound fails, stop.** Do not optimise silently. Report the four figures to the controller, which escalates to the maintainer (spec §14.1: "blocks the release until it is explained and the maintainer accepts it").
  - A `PUSH_OWN_CONST_INSTVAR` twin (spec §16) is the one pre-approved remedy for const_read; it is measured the same way.
- [ ] **Step 4: Check the plain path for regressions.**
  - Run `python3 benchmarks/compare_builds.py --json $S/plain-path.json old=$S/ref-0161/build_release/protost new=$WT/build_release/protost --libdir old=$S/pc-361/lib --libdir new=$S/pc-361/lib`.
  - Then run `perf stat -r 3 -e cycles` of `benchmarks/micro/instances.st` and `vm_ops.st` on each build, with fresh copies.
  - Report every figure. A slowdown above noise in `instances.st` (D-e's `new` send) is reported, not hidden.
- [ ] **Step 5: Commit** the benchmark, the script, README and the report: `git commit -m "bench(explicit-state): const_read twin and the 14.1 gate; 0.17.0 figures"`

---

### Task 13: Release protoST 0.17.0

**Files:**
- Modify:
  - `CMakeLists.txt` (`project(protoST VERSION 0.17.0 …)`);
  - `CHANGELOG.md`: a new `## 0.17.0 — <date>` with sections Added, Changed (the `STORE_INSTVAR` error class, and `setInstVar:from:to:` checked and deprecated), Deprecated (W1), Performance (the four §14.1 figures and the plain-path figures), Tests and Follow-up (protoBaseST 0.5.0, §13.2);
  - `docs/STATUS.md` (the header line: version, date, protoCore 3.6.1, protoIO 0.7.1);
  - `README.md`, `docs/INSTALLATION.md`, `docs/tutorial/01-introduction.md:169` and `docs/tutorial/12-tooling.md:39`: every `0.16.1` that names the current release.
- Keep `conformance-allow.txt` in step with moved lines.

- [ ] **Step 1: Bring the branch up to date.** Merge or rebase `origin/main`: it is ahead of the spec branch by documentation commits only, and the spec PR #25 is being merged. Resolve conflicts. Then run `git log --oneline origin/main..HEAD`.
- [ ] **Step 2: Make the version and documentation edits.** Check `grep -rn "0\.16\.1" README.md docs/INSTALLATION.md docs/tutorial` shows only historical mentions.
- [ ] **Step 3: Verify locally, sequentially, each into a log in `$S`.**
  1. A full `ctest` on `build_release`, compared with Task 1's count plus the new tests.
  2. `ctest -L tsan` on `build_tsan`.
  3. `python3 ../protoCore/scripts/conformance/check_static.py --repo .` is clean (run it from `$WT`, with `../protoCore` reachable; otherwise use the path of the 3.6.1 source extract).
  4. `ldd` on both binaries.
  5. `protost --version` prints `protoST 0.17.0`.
- [ ] **Step 4: Commit the release changes.** `git commit -m "Release protoST 0.17.0: explicit state I, instance-variable modifiers"`
- [ ] **Step 5: Push and open the PR.** Run `git push -u origin feat/explicit-state-0170`, then `gh pr create` with a body summarising the release and the four figures, ending with the attribution line from the session reminder.
  - Wait for green CI on every job: Linux, assertions, the protoCore 3.6.1 floor, ThreadSanitizer, macOS arm64, Windows MSVC.
  - A red job is diagnosed and fixed on the branch; never re-run blindly.
- [ ] **Step 6: Merge, tag and release.**
  - Merge the PR (merge commit, as #23).
  - Check that the main CI and Cross-platform runs are green.
  - `git tag -a v0.17.0 -m "protoST 0.17.0"` on the merge commit, then `git push origin v0.17.0`.
  - `gh release create v0.17.0 --title "protoST 0.17.0" --latest`, with the CHANGELOG section as notes.
- [ ] **Step 7: Build the .deb** in the `ubuntu:24.04`-based `pc-deb-build:24.04` image (build it from `ubuntu:24.04` with the 0.16.1 build dependencies if it is missing).
  - Use fresh clones of `v0.17.0` and protoIO `v0.7.1`, with `-DPROTOCORE_REQUIRE_PACKAGE=ON`, against `protoCore-3.6.1-Linux.deb`.
  - Use `--network host` if the FetchContent download fails on DNS.
  - Use a new build directory name; never reuse a root-owned one.
  - Check: `dpkg-deb -I` shows `Depends: protocore (>= 3.6.1)`; the entry count matches 0.16.1's layout (195) plus or minus documented new files.
- [ ] **Step 8: Smoke test in a clean `ubuntu:24.04` container.**
  - `protost --version` prints 0.17.0.
  - The 0.16.1 smoke program passes.
  - The §6.4 `Account` example prints its result.
  - A `setInstVar:from:to:` call prints W1 once.
- [ ] **Step 9: Publish the .deb and verify its digest.**
  - `gh release upload v0.17.0 protost-0.17.0-Linux.deb`.
  - Download the asset and check its sha256 matches the local file and GitHub's asset digest.
  - Record the digest in the final report.
- [ ] **Step 10: Final report to the controller.** Include:
  - the PR, the merge commit and the tag object;
  - the deb digest;
  - the test counts;
  - the four §14.1 figures;
  - the protoBaseST follow-up;
  - every ruling of this plan that the spec did not fix (D-a…D-j, G1–G4).

---

## Review Focus

1. **A late-installed method on a modified variable** (a message-form class, a subclass or extension of an imported class): it gets the same M1/M2/M4 refusals and the same opcodes as a compiled one, at install time. The test is in Task 3.
2. **A constant assigned in an inlined block of `initialize` whose receiver is not a Boolean**: the de-optimised copy is a real block, but it must store the constant, not refuse it. The test is in Task 3.
3. **An `update:with:` block left by a non-local return or an exception**: the M7 depth must return to zero, or every later `wait` on that thread fails. The test is in Task 7.
4. **A class declared in one REPL input or module, with methods in a later one**: the compiler must see the modifiers through `knownClasses_` and the shared registry. The tests are in Tasks 2 and 3.
5. **A constant read inside a run of assignments**: the read can signal, so it must end the run, and a handler must see the assignments before it published. The test is in Task 5.

## Spec gaps ruled in this plan

- **G1.** Modifiers apply to instance variables only. Class-instance variables and class variables take none.
- **G2.** `compareAndSet: #w from: nil to: nil` on a write-once variable writes nothing and answers whether the variable is unset.
- **G3.** `const_read.st` cannot run on 0.16.1, because it does not parse there. So of §14.1's "four figures", const_read on 0.16.1 is reported as "not runnable", and the gate uses the three runnable ones.
- **G4.** `Object >> inspect` does not check constant reads. It shows `<unset constant>`, as the debugger row of §6.2 does (D-h).
- Also D-e (the `new` check costs one primitive send) and D-j (no DAP change).
