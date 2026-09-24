// Track S — white-box tests for the ProtoMap-backed Set and Dictionary.
//
// These tests read the backing `ProtoMap` directly, and that is the point.
// protoScala's P1 work found that the two slot kinds protoCore's
// hashed-collection helper offers — an identity slot keyed by the key object's
// own word, and a hashed slot keyed by a SmallInteger word carrying the hash —
// are observationally equivalent through a language: put a key, get it back,
// and you cannot tell which path it took. A misclassification is therefore
// invisible to every black-box test, so a white-box one is the only kind that
// can pin the choice down.
//
// protoST's choice is that EVERY key takes the hashed, value-equality path:
// `stIsIdentityKey` always answers false, so membership is always decided by
// `stKeyEquals` (identity, then the language's `=`) and there is exactly one
// rule. `slot kinds` below fails if that is ever quietly changed.
//
// The second invisible thing is the entry count. `ProtoMap::getSize` counts
// SLOTS, and one slot holds every key whose hash collides, so a `size`
// implemented as the slot count is right until two keys collide and then
// silently wrong — and the language cannot tell a collision from a
// non-collision either. `__size__` therefore carries the count, and
// `entry count` below checks it against a recount of the real entries under a
// forced collision.

#include <catch2/catch_all.hpp>

#include "protoST/STRuntime.h"
#include "frontend/Parser.h"
#include "frontend/Compiler.h"
#include "runtime/BytecodeModule.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace {

struct CollectionHost {
    std::vector<std::unique_ptr<protoST::BytecodeModule>> kept;
    protoST::STRuntime rt;

    const proto::ProtoObject* run(const std::string& src) {
        protoST::Parser P(src.c_str());
        auto ast = P.parseModule();
        REQUIRE(P.errors().empty());
        protoST::Compiler C;
        auto bc = C.compileModule(*ast);
        REQUIRE(!C.hasErrors());
        const proto::ProtoObject* r = rt.runTopLevel(*bc);
        kept.push_back(std::move(bc));
        return r;
    }
};

const proto::ProtoObject* attributeOf(proto::ProtoContext* ctx,
                                      const proto::ProtoObject* obj,
                                      const char* name) {
    return obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
}

// What a walk of the backing map found.
struct MapShape {
    unsigned long slots = 0;        // ProtoMap::getSize — slots, not entries
    unsigned long entries = 0;      // key/value pairs, counted through the buckets
    unsigned long identitySlots = 0;// slots keyed by the key object itself
};

void inspectSlot(proto::ProtoContext* ctx, void* self,
                 const proto::ProtoObject* slotKey,
                 const proto::ProtoObject* slotValue) {
    auto* shape = static_cast<MapShape*>(self);
    // The helper's encoding (protoCore.h, hashedPut): a hashed slot's key is a
    // SmallInteger word carrying the hash and its value is a flat
    // [k0, v0, k1, v1, ...] ProtoList; an identity slot's key IS the key.
    if (slotKey && slotKey->isInteger(ctx)) {
        const proto::ProtoList* bucket = slotValue ? slotValue->asList(ctx) : nullptr;
        REQUIRE(bucket != nullptr);
        shape->entries += static_cast<unsigned long>(bucket->getSize(ctx)) / 2;
    } else {
        ++shape->identitySlots;
        ++shape->entries;
    }
}

MapShape shapeOf(proto::ProtoContext* ctx, const proto::ProtoObject* coll) {
    const proto::ProtoObject* raw = attributeOf(ctx, coll, "__data__");
    REQUIRE(raw != nullptr);
    REQUIRE(raw != PROTO_NONE);
    const proto::ProtoMap* map = raw->asMap(ctx);
    REQUIRE(map != nullptr);
    MapShape shape;
    shape.slots = map->getSize(ctx);
    map->processElements(ctx, &shape, inspectSlot);
    return shape;
}

long long storedSize(proto::ProtoContext* ctx, const proto::ProtoObject* coll) {
    const proto::ProtoObject* n = attributeOf(ctx, coll, "__size__");
    REQUIRE(n != nullptr);
    REQUIRE(n != PROTO_NONE);
    return n->asLong(ctx);
}

// 2^60 and 2^60+1 are distinct integers that round to the same double, and the
// numeric key hash folds the double, so they land in one slot.
const char* const kCollidingKeys =
    "a := 1. 1 to: 60 do: [ :i | a := a * 2 ]. b := a + 1. ";

} // namespace

TEST_CASE("Track S: a Set's entry count is not its slot count",
          "[collections][hashed][whitebox]") {
    CollectionHost h;
    const proto::ProtoObject* set = h.run(
        std::string(kCollidingKeys) +
        "s := Set new. s add: a. s add: b. s");
    REQUIRE(set != nullptr);
    proto::ProtoContext* ctx = h.rt.rootCtx();

    const MapShape shape = shapeOf(ctx, set);
    // The collision is the whole point of the fixture: if these two keys ever
    // stop sharing a slot the test is no longer testing what it claims to.
    REQUIRE(shape.slots == 1);
    REQUIRE(shape.entries == 2);
    REQUIRE(storedSize(ctx, set) == 2);
}

TEST_CASE("Track S: every hashed-collection key takes the value-equality path",
          "[collections][hashed][whitebox]") {
    CollectionHost h;
    // Deliberately mixed: a plain object (whose `=` is identity), a symbol, a
    // string, an integer, a float and nil. None of them may end up in an
    // identity slot, because protoST has one membership rule for all of them.
    const proto::ProtoObject* set = h.run(
        "o := Object new. "
        "s := Set new. "
        "s add: o. s add: #sym. s add: 'str'. s add: 7. s add: 2.5. s add: nil. "
        "s");
    REQUIRE(set != nullptr);
    proto::ProtoContext* ctx = h.rt.rootCtx();

    const MapShape shape = shapeOf(ctx, set);
    REQUIRE(shape.entries == 6);
    REQUIRE(shape.identitySlots == 0);
    REQUIRE(storedSize(ctx, set) == 6);
}

TEST_CASE("Track S: a Dictionary's stored count tracks adds and removes",
          "[collections][hashed][whitebox]") {
    CollectionHost h;
    const proto::ProtoObject* dict = h.run(
        std::string(kCollidingKeys) +
        "d := Dictionary new. "
        "d at: a put: 'a'. d at: b put: 'b'. "     // one slot, two entries
        "d at: #x put: 1. d at: 'y' put: 2. "
        "d at: a put: 'a2'. "                       // overwrite, not an insert
        "d at: 1 put: 'one'. d at: 1.0 put: 'onef'. " // equal keys: one entry
        "d removeKey: #x. "
        "d");
    REQUIRE(dict != nullptr);
    proto::ProtoContext* ctx = h.rt.rootCtx();

    const MapShape shape = shapeOf(ctx, dict);
    // a, b, 'y', 1 — #x was removed, the a-overwrite added nothing, and 1.0 is
    // the same key as 1.
    REQUIRE(shape.entries == 4);
    REQUIRE(shape.identitySlots == 0);
    REQUIRE(storedSize(ctx, dict) == 4);
    // And the count is genuinely not the slot count: a and b share one.
    REQUIRE(shape.slots == 3);
}

TEST_CASE("Track S: a Set's stored count tracks removals through a collision bucket",
          "[collections][hashed][whitebox]") {
    CollectionHost h;
    const proto::ProtoObject* set = h.run(
        std::string(kCollidingKeys) +
        "s := Set new. s add: a. s add: b. s add: 3. "
        "s remove: a. "
        "s");
    REQUIRE(set != nullptr);
    proto::ProtoContext* ctx = h.rt.rootCtx();

    const MapShape shape = shapeOf(ctx, set);
    REQUIRE(shape.entries == 2);
    REQUIRE(storedSize(ctx, set) == 2);
    REQUIRE(shape.identitySlots == 0);
}
