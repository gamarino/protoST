// Class instance templates (src/runtime/InstanceTemplate.h).
//
// `basicNew` (and so `new`) derives every instance of a class from one
// immutable template object, a child of the class whose attribute shape
// already names every instance variable the class declares or inherits, with
// every slot unset. protoCore's ProtoObject::newClone copies the template's
// parent link and shape, so all instances of a class share one parent link
// and one ProtoShape, and assigning an instance variable replaces a value
// instead of extending the object's shape.
//
// protoCore's public API does not expose an object's shape, so these tests
// observe it through what it costs: replacing the value of a name the shape
// already holds allocates fewer cells than adding a name, and the cost of a
// replacement does not depend on which name is replaced. They pin down:
//
//   * that a class has one cached template, a child of the class whose
//     unset slots are invisible (hasOwnAttribute answers false);
//   * that the template's shape holds the inherited variables as well as the
//     class's own;
//   * that instances made by `new` are clones of it: two instances answer
//     the template's replacement cost for every variable;
//   * that re-declaring a class's variables, or rebuilding the class with
//     addBehavior:, gives it (and its subclasses) a new template.

#include <catch2/catch_all.hpp>

#include <memory>
#include <string>
#include <vector>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "runtime/InstanceTemplate.h"
#include "runtime/NativeError.h"
#include "protoCore.h"

namespace {

struct Host {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    const proto::ProtoObject* run(const std::string& src) {
        protoST::Parser P(src.c_str());
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        if (C.hasErrors()) FAIL(C.errors().front());
        const proto::ProtoObject* r = nullptr;
        std::string error;
        try {
            r = rt.runTopLevel(*bc);
        } catch (const std::exception& e) {
            error = protoST::nativeErrorText(e);
        }
        if (!error.empty()) FAIL("runTopLevel failed: " << error);
        kept.push_back(std::move(bc));
        return r;
    }
    proto::ProtoContext* ctx() { return rt.rootCtx(); }
    const proto::ProtoString* sym(const char* s) {
        return proto::ProtoString::createSymbol(ctx(), s);
    }
    const proto::ProtoObject* global(const char* name) {
        const proto::ProtoObject* g = rt.globals()->getAttribute(ctx(), sym(name));
        REQUIRE(g != nullptr);
        REQUIRE(g != PROTO_NONE);
        return g;
    }
    // The cells one immutable attribute write on `o` allocates.
    long long writeCost(const proto::ProtoObject* o, const char* key) {
        proto::ProtoContext* c = ctx();
        const proto::ProtoObject* v = c->fromLong(7);
        const proto::proto_ulong before = c->allocatedCellsCount;
        const proto::ProtoObject* w = o->setAttribute(c, sym(key), v);
        const proto::proto_ulong after = c->allocatedCellsCount;
        REQUIRE(w != nullptr);
        return static_cast<long long>(after - before);
    }
};

const char* kDefs = R"(Object subclass: #Base instanceVariableNames: 'a b'.
Base subclass: #Sub instanceVariableNames: 'c d'.
)";

} // namespace

TEST_CASE("Templates: a class has one cached template, a child of the class",
          "[templates]") {
    Host h;
    h.run(kDefs);
    proto::ProtoContext* ctx = h.ctx();
    const proto::ProtoObject* sub = h.global("Sub");
    const proto::ProtoObject* t1 = protoST::instanceTemplate(h.rt, ctx, sub);
    REQUIRE(t1 != nullptr);
    CHECK(protoST::instanceTemplate(h.rt, ctx, sub) == t1);
    CHECK(t1->getFirstParent(ctx) == sub);
    // Every slot is unset: nothing an instance has not assigned is visible.
    for (const char* k : {"_iv_a", "_iv_b", "_iv_c", "_iv_d"})
        CHECK(t1->hasOwnAttribute(ctx, h.sym(k)) == PROTO_FALSE);
}

TEST_CASE("Templates: a subclass template holds its inherited variables",
          "[templates]") {
    Host h;
    h.run(kDefs);
    proto::ProtoContext* ctx = h.ctx();
    const proto::ProtoObject* t = protoST::instanceTemplate(h.rt, ctx, h.global("Sub"));
    REQUIRE(t != nullptr);
    const long long added = h.writeCost(t, "_iv_notDeclared");
    const long long own = h.writeCost(t, "_iv_c");
    INFO("replace own " << own << " cells, add a name " << added << " cells");
    CHECK(own < added);
    for (const char* k : {"_iv_a", "_iv_b", "_iv_d"}) {
        INFO(k);
        CHECK(h.writeCost(t, k) == own);
    }
}

TEST_CASE("Templates: two instances share the template's shape", "[templates]") {
    Host h;
    h.run(kDefs);
    proto::ProtoContext* ctx = h.ctx();
    for (const char* inst : {"X", "Y"})
        h.rt.globals()->setAttribute(ctx, h.sym(inst), h.run("Sub new."));
    const proto::ProtoObject* t = protoST::instanceTemplate(h.rt, ctx, h.global("Sub"));
    const long long replace = h.writeCost(t, "_iv_a");
    const long long added = h.writeCost(t, "_iv_notDeclared");
    REQUIRE(replace < added);
    for (const char* inst : {"X", "Y"}) {
        // Read the instance through an immutable copy: newClone keeps the
        // parent link and the shape of the receiver's current version.
        const proto::ProtoObject* snap = h.global(inst)->newClone(ctx, false);
        CHECK(snap->getFirstParent(ctx) == h.global("Sub"));
        for (const char* k : {"_iv_a", "_iv_b", "_iv_c", "_iv_d"}) {
            INFO(inst << " " << k);
            CHECK(h.writeCost(snap, k) == replace);
        }
        CHECK(h.writeCost(snap, "_iv_notDeclared") == added);
    }
    // Instances stay mutable, and a write is visible through the instance.
    h.run("X instVarNamed: 'a' put: 42.");
    const proto::ProtoObject* a = h.global("X")->getAttribute(ctx, h.sym("_iv_a"));
    REQUIRE(a != nullptr);
    CHECK(a->asLong(ctx) == 42);
    CHECK(h.global("Y")->hasOwnAttribute(ctx, h.sym("_iv_a")) == PROTO_FALSE);
}

TEST_CASE("Templates: re-declaring a class's variables rebuilds the templates",
          "[templates]") {
    Host h;
    h.run(kDefs);
    proto::ProtoContext* ctx = h.ctx();
    const proto::ProtoObject* base = h.global("Base");
    const proto::ProtoObject* sub = h.global("Sub");
    const proto::ProtoObject* before = protoST::instanceTemplate(h.rt, ctx, sub);
    REQUIRE(before != nullptr);
    // A new variable declared on the superclass, after the subclass's
    // template was built.
    h.run("Base __setInstVarNames: 'a b e'.");
    REQUIRE(h.global("Base") == base);
    const proto::ProtoObject* after = protoST::instanceTemplate(h.rt, ctx, sub);
    REQUIRE(after != nullptr);
    CHECK(after != before);
    CHECK(h.writeCost(after, "_iv_e") == h.writeCost(after, "_iv_a"));
    CHECK(h.writeCost(after, "_iv_e") < h.writeCost(after, "_iv_notDeclared"));
}

TEST_CASE("Templates: a class rebuilt by addBehavior: gets its own template",
          "[templates]") {
    Host h;
    h.run(kDefs);
    h.run("Object subclass: #Mixin. Mixin >> hello ^ 7.");
    proto::ProtoContext* ctx = h.ctx();
    const proto::ProtoObject* old = h.global("Sub");
    REQUIRE(protoST::instanceTemplate(h.rt, ctx, old) != nullptr);
    h.run("Sub addBehavior: Mixin.");
    const proto::ProtoObject* rebuilt = h.global("Sub");
    REQUIRE(rebuilt != old);
    const proto::ProtoObject* t = protoST::instanceTemplate(h.rt, ctx, rebuilt);
    REQUIRE(t != nullptr);
    CHECK(t->getFirstParent(ctx) == rebuilt);
    const proto::ProtoObject* r = h.run("Sub new hello.");
    REQUIRE(r != nullptr);
    CHECK(r->asLong(ctx) == 7);
}

namespace {
// Cells allocated by making an instance of `cls` (from its template, or as a
// plain mutable child) and assigning `n` of `keys` in one group, as an
// initializer's write group does (ProtoObject::setAttributes on the mutable
// instance). The template is built before counting.
long long constructionCells(Host& h, const proto::ProtoObject* cls, bool fromTemplate,
                            const std::vector<const char*>& keys) {
    proto::ProtoContext* c = h.ctx();
    REQUIRE(protoST::instanceTemplate(h.rt, c, cls) != nullptr);
    std::vector<const proto::ProtoString*> names;
    std::vector<const proto::ProtoObject*> values;
    for (const char* k : keys) {
        names.push_back(h.sym(k));
        values.push_back(c->fromLong(static_cast<long long>(names.size())));
    }
    const proto::proto_ulong before = c->allocatedCellsCount;
    const proto::ProtoObject* inst = fromTemplate ? protoST::newInstance(h.rt, c, cls)
                                                  : cls->newChild(c, /*isMutable=*/true);
    inst->setAttributes(c, static_cast<unsigned>(names.size()), names.data(), values.data());
    const proto::proto_ulong after = c->allocatedCellsCount;
    return static_cast<long long>(after - before);
}
} // namespace

TEST_CASE("Templates: a templated construction allocates no more cells than a plain child",
          "[templates][cells]") {
    Host h;
    h.run(kDefs);
    const proto::ProtoObject* base = h.global("Base");
    const proto::ProtoObject* sub = h.global("Sub");
    const long long b2t = constructionCells(h, base, true, {"_iv_a", "_iv_b"});
    const long long b2c = constructionCells(h, base, false, {"_iv_a", "_iv_b"});
    const long long s4t = constructionCells(h, sub, true, {"_iv_a", "_iv_b", "_iv_c", "_iv_d"});
    const long long s4c = constructionCells(h, sub, false, {"_iv_a", "_iv_b", "_iv_c", "_iv_d"});
    WARN("cells per construction: Base (2 ivars) template " << b2t << ", plain child " << b2c
         << "; Sub (4 ivars) template " << s4t << ", plain child " << s4c);
    CHECK(b2t <= b2c);
    CHECK(s4t <= s4c);
}
