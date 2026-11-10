#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/BytesSupport.h"
#include "primitives/CollectionSupport.h"
#include "primitives/ProtoioErrors.h"
#include "primitives/StringSupport.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "protoCore.h"

#include <protoio/error.h>
#include <protoio/msgpack.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace protoST {

// Defined in object_prims.cpp: run a method by name, as a send does.
const proto::ProtoObject* sendDynamic(STRuntime& rt, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* recv,
                                      const proto::ProtoString* selector,
                                      const proto::ProtoObject* const* args, int argc,
                                      bool* understood);

// MessagePack (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md,
// section 4.5), bound to protoio::msgpack. protoIO has no value tree: encoding
// walks the protoST object graph into a protoio Writer, and decoding builds
// protoST objects from the events protoio::msgpack::read reports. Neither
// recurses on the C++ stack, so a hostile document (or a structure nested deep
// by accident) cannot overflow it.
//
// The classes MessagePackMap and MessagePackExt live in lib/kernel/msgpack.st;
// lib/kernel/msgpack.st passes them to every primitive, so C++ never looks a
// global up by name.

namespace {

using PO = proto::ProtoObject;

#define PRIM(NAME) const PO* NAME(STRuntime& rt, proto::ProtoContext* ctx, const PO* r, \
                                   const PO* const* a, int argc)
#define ARGS(N, SEL) if (argc != (N)) throw std::runtime_error(SEL " expects " #N " argument(s)"); \
                     (void) rt; (void) r; (void) a

// The depth the decoder accepts (protoIO's Limits::maxDepth default). The
// encoder refuses what the decoder would refuse.
const size_t kMaxDepth = protoio::msgpack::Limits{}.maxDepth;


bool booleanArg(const PO* v, const char* who) {
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    throw std::runtime_error(std::string(who) + ": a Boolean was expected");
}

// The answer to a unary accessor of one of our own kernel classes. These answer
// an instance variable, which the receiver keeps reachable.
const PO* accessor(STRuntime& rt, proto::ProtoContext* ctx, const PO* recv, const char* selector) {
    bool understood = false;
    const PO* v = sendDynamic(rt, ctx, recv, proto::ProtoString::createSymbol(ctx, selector),
                              nullptr, 0, &understood);
    if (!understood) throw std::runtime_error(std::string("MessagePack: the object does not understand ") + selector);
    return v ? v : PROTO_NONE;
}

std::string classNameOf(STRuntime& rt, proto::ProtoContext* ctx, const PO* v) {
    bool understood = false;
    const PO* cls = sendDynamic(rt, ctx, v, proto::ProtoString::createSymbol(ctx, "class"),
                                nullptr, 0, &understood);
    const PO* name = (understood && cls && cls != PROTO_NONE)
        ? cls->getAttribute(ctx, rt.bootstrap().sym.className) : nullptr;
    const proto::ProtoString* s = (name && name != PROTO_NONE) ? name->asString(ctx) : nullptr;
    return s ? s->toStdString(ctx) : std::string("object of unknown class");
}

// ---- encoding ---------------------------------------------------------------
//
// The graph is walked with an explicit stack of frames. A frame is a container
// being written and the items still to write. `path` holds the containers of
// the frames now open: meeting one of them again is a cycle (a container that
// appears twice without being inside itself is shared, not cyclic, and encodes
// twice).
//
// Rooting contract. The walk allocates (LargeInteger digits, sends, the lists
// below), and the items of a frame are raw pointers read from a snapshot (the
// container's immutable list or map) that another actor may replace at any
// moment, dropping the snapshot's last reference. So the encoder owns ONE
// rooted structure, `roots_`, a ProtoList held by a single TransientPin: when
// a frame opens, the snapshot its items point into is appended to it, and when
// the frame closes it is removed (frames are strictly LIFO). A
// MessagePackMap's flattened key/value items are not stored anywhere in the
// graph, and an Association's key may be replaced by another actor, so they
// are first copied into a fresh list and that list is rooted instead. The
// root passed in by the caller is reachable through the primitive's argument.
class Encoder {
public:
    Encoder(STRuntime& rt, proto::ProtoContext* ctx, const PO* mapClass, const PO* extClass)
        : rt_(rt), ctx_(ctx), mapClass_(mapClass), extClass_(extClass),
          roots_(ctx->newList()), pin_(ctx, reinterpret_cast<const PO*>(roots_)) {}

    std::string run(const PO* root) {
        emit(root);
        while (!frames_.empty()) {
            Frame& f = frames_.back();
            if (f.next == f.items.size()) {
                path_.erase(f.container);
                frames_.pop_back();
                roots_ = roots_->removeLast(ctx_);
                pin_.reset(reinterpret_cast<const PO*>(roots_));
                continue;
            }
            const PO* item = f.items[f.next++];
            emit(item);   // may push a frame, which invalidates `f`
        }
        return w_.take();
    }

private:
    struct Frame {
        const PO* container;
        std::vector<const PO*> items;
        size_t next = 0;
    };

    // Opens a frame. `snapshot` is the list or map the items point into (or the
    // list the items were copied to); it stays rooted until the frame closes.
    void open(const PO* container, const PO* snapshot, std::vector<const PO*>&& items, bool map) {
        if (path_.count(container)) throw std::runtime_error("MessagePack: cyclic structure");
        if (frames_.size() >= kMaxDepth)
            throw std::runtime_error("MessagePack: nesting deeper than " + std::to_string(kMaxDepth)
                                     + " levels");
        const size_t entries = map ? items.size() / 2 : items.size();
        if (entries > 0xFFFFFFFFull)
            throw std::runtime_error("MessagePack: a container of more than 2^32 - 1 elements");
        if (map) w_.beginMap(static_cast<uint32_t>(entries));
        else w_.beginArray(static_cast<uint32_t>(entries));
        TransientPin pinSnapshot(ctx_, snapshot);
        roots_ = roots_->appendLast(ctx_, snapshot);
        pin_.reset(reinterpret_cast<const PO*>(roots_));
        path_.insert(container);
        frames_.push_back(Frame{container, std::move(items), 0});
    }

    void emitInteger(const PO* v) {
        if (proto::isSmallInt(v)) { w_.integer(v->asLong(ctx_)); return; }
        const std::string digits = v->asIntegerString(ctx_, 10)->toStdString(ctx_);
        errno = 0;
        char* end = nullptr;
        if (digits[0] == '-') {
            const long long n = std::strtoll(digits.c_str(), &end, 10);
            if (errno != ERANGE) { w_.integer(n); return; }
        } else {
            const unsigned long long n = std::strtoull(digits.c_str(), &end, 10);
            if (errno != ERANGE) {
                if (n <= static_cast<unsigned long long>(INT64_MAX)) w_.integer(static_cast<int64_t>(n));
                else w_.unsignedInteger(n);
                return;
            }
        }
        throw std::runtime_error("MessagePack cannot encode the Integer " + digits
                                 + ": it is outside -2^63 .. 2^64 - 1");
    }

    // The Associations of a MessagePackMap, flattened to key, value, ... and
    // copied into a fresh list (answered through `rooted`) that the caller roots.
    std::vector<const PO*> pairsOf(const PO* m, const PO*& rooted) {
        const PO* list = accessor(rt_, ctx_, m, "associations");
        if (!list || list == PROTO_NONE || !isArrayOrOrdered(rt_, ctx_, list))
            throw std::runtime_error("MessagePackMap: its associations must be an Array");
        const proto::ProtoList* l = arrayOrOrderedElements(ctx_, list);
        TransientPin pinList(ctx_, asRootable(l));
        const size_t n = static_cast<size_t>(l->getSize(ctx_));
        std::vector<const PO*> out;
        out.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) {
            const PO* assoc = l->getAt(ctx_, listIndex(i));
            if (!isAssociation(rt_, ctx_, assoc))
                throw std::runtime_error("MessagePackMap: every element must be an Association");
            out.push_back(associationKey(ctx_, assoc));
            out.push_back(associationValue(ctx_, assoc));
        }
        const proto::ProtoList* flat = out.empty() ? ctx_->newList()
            : ctx_->newList(static_cast<unsigned>(out.size()), out.data());
        rooted = asRootable(flat);
        return out;
    }

    void emitExt(const PO* e) {
        const PO* t = accessor(rt_, ctx_, e, "type");
        const PO* d = accessor(rt_, ctx_, e, "data");
        if (!t || t == PROTO_NONE || !proto::isSmallInt(t) || t->asLong(ctx_) < -128 || t->asLong(ctx_) > 127)
            throw std::runtime_error("MessagePackExt: the ext type must be an Integer from -128 to 127");
        w_.ext(static_cast<int8_t>(t->asLong(ctx_)), bytesView(ctx_, d));
    }

    void emit(const PO* v) {
        if (!v || v == PROTO_NONE) { w_.nil(); return; }
        if (v == PROTO_TRUE) { w_.boolean(true); return; }
        if (v == PROTO_FALSE) { w_.boolean(false); return; }
        if (isCharacterValue(ctx_, v)) unsupported(v);
        if (v->isInteger(ctx_)) { emitInteger(v); return; }
        if (v->isFloat(ctx_)) { w_.float64(v->asDouble(ctx_)); return; }
        if (v->isByteBuffer(ctx_)) { w_.bin(bytesView(ctx_, v)); return; }
        if (const proto::ProtoString* s = v->asString(ctx_)) { w_.str(s->toStdString(ctx_)); return; }
        const PO* objectProto = rt_.bootstrap().objectProto;
        if (inheritsFromProto(ctx_, v, mapClass_, objectProto)) {
            const PO* rooted = nullptr;
            std::vector<const PO*> items = pairsOf(v, rooted);
            open(v, rooted, std::move(items), true);
            return;
        }
        if (inheritsFromProto(ctx_, v, extClass_, objectProto)) { emitExt(v); return; }
        if (isDictionary(rt_, ctx_, v)) {
            std::vector<std::pair<const PO*, const PO*>> entries;
            const PO* snapshot = dictionaryEntries(ctx_, v, entries);
            std::vector<const PO*> flat;
            flat.reserve(entries.size() * 2);
            for (const auto& e : entries) { flat.push_back(e.first); flat.push_back(e.second); }
            open(v, snapshot, std::move(flat), true);
            return;
        }
        if (isArrayOrOrdered(rt_, ctx_, v)) {
            const proto::ProtoList* l = arrayOrOrderedElements(ctx_, v);
            const size_t n = static_cast<size_t>(l->getSize(ctx_));
            std::vector<const PO*> items;
            items.reserve(n);
            for (size_t i = 0; i < n; ++i) items.push_back(l->getAt(ctx_, listIndex(i)));
            open(v, asRootable(l), std::move(items), false);
            return;
        }
        unsupported(v);
    }

    [[noreturn]] void unsupported(const PO* v) {
        throw std::runtime_error("MessagePack cannot encode an instance of " + classNameOf(rt_, ctx_, v));
    }

    STRuntime& rt_;
    proto::ProtoContext* ctx_;
    const PO* mapClass_;
    const PO* extClass_;
    protoio::msgpack::Writer w_;
    std::vector<Frame> frames_;
    std::unordered_set<const PO*> path_;
    const proto::ProtoList* roots_;   // the snapshots of the open frames, in order
    TransientPin pin_;                // roots_'s single root
};

// aClass __msgpackEncode: anObject map: MessagePackMap ext: MessagePackExt -> a ByteArray.
PRIM(prim_MsgpackEncode) {
    ARGS(3, "__msgpackEncode:map:ext:");
    try {
        Encoder enc(rt, ctx, a[1], a[2]);
        return makeBytes(ctx, enc.run(a[0]));
    } catch (const protoio::Error& e) {
        raiseProtoioError(e, "MessagePack encode");
    }
}

// ---- decoding ---------------------------------------------------------------
//
// protoio reports a document as events in order. Finished values are kept on
// a stack that is one immutable ProtoList, held by a single TransientPin: a
// scalar is pushed as it arrives, and when a container's last child has
// arrived the children are taken off the top of the stack and replaced by the
// container. The open containers are tracked in a plain C++ vector (they hold
// only counts, no heap objects), so nothing the collector must see lives
// outside the pinned list or a short-lived local pin.
class Decoder : public protoio::msgpack::Handler {
public:
    Decoder(STRuntime& rt, proto::ProtoContext* ctx, bool ordered, const PO* mapClass, const PO* extClass)
        : rt_(rt), ctx_(ctx), ordered_(ordered), mapClass_(mapClass), extClass_(extClass),
          stack_(ctx->newList()),
          pin_(ctx, reinterpret_cast<const PO*>(stack_)) {}

    const PO* result() const {
        if (stack_->getSize(ctx_) != 1) throw std::runtime_error("MessagePack: internal error: no single result");
        return stack_->getAt(ctx_, 0);
    }

    void nil() override { push(PROTO_NONE); }
    void boolean(bool v) override { push(v ? PROTO_TRUE : PROTO_FALSE); }
    void integer(int64_t v) override { push(ctx_->fromLong(v)); }
    void unsignedInteger(uint64_t v) override {
        push(ctx_->fromString(std::to_string(v).c_str(), 10));
    }
    void float64(double v) override { push(ctx_->fromDouble(v)); }
    void str(std::string_view s) override { push(makeString(ctx_, s)); }
    void bin(std::string_view b) override { push(makeBytes(ctx_, b)); }
    void beginArray(uint32_t n) override { begin(false, n); }
    void beginMap(uint32_t n) override { begin(true, n); }
    void ext(int8_t type, std::string_view data) override {
        const PO* bytes = makeBytes(ctx_, data);
        TransientPin pinBytes(ctx_, bytes);
        const PO* t = ctx_->fromLong(type);
        TransientPin pinType(ctx_, t);
        const PO* args[2] = {t, bytes};
        bool understood = false;
        const PO* e = sendDynamic(rt_, ctx_, extClass_,
                                  proto::ProtoString::createSymbol(ctx_, "type:data:"), args, 2, &understood);
        if (!understood) throw std::runtime_error("MessagePackExt class does not understand type:data:");
        push(e);
    }

private:
    struct Open {
        bool map;
        size_t want;   // children still to come, counted in stack slots
        size_t base;   // stack size when the container began
    };

    // Appends `v` to the stack and closes every container that is now complete.
    void push(const PO* v) {
        append(v);
        settle();
    }

    void append(const PO* v) {
        TransientPin pinValue(ctx_, v);
        stack_ = stack_->appendLast(ctx_, v);
        pin_.reset(reinterpret_cast<const PO*>(stack_));
    }

    void begin(bool map, uint32_t n) {
        open_.push_back(Open{map, map ? 2 * static_cast<size_t>(n) : static_cast<size_t>(n),
                             static_cast<size_t>(stack_->getSize(ctx_))});
        settle();   // an empty container is complete at once
    }

    void settle() {
        while (!open_.empty()) {
            const Open o = open_.back();
            if (static_cast<size_t>(stack_->getSize(ctx_)) - o.base < o.want) return;
            open_.pop_back();
            const PO* built = build(o);
            TransientPin pinBuilt(ctx_, built);
            // Take the children off the top. Rebuilding the part below them is
            // one pass over `base` elements and dropping them one by one is
            // `want` passes of O(log n); take the cheaper.
            if (o.base < o.want) stack_ = stack_->getSlice(ctx_, 0, listIndex(o.base));
            else for (size_t i = 0; i < o.want; ++i) stack_ = stack_->removeLast(ctx_);
            pin_.reset(reinterpret_cast<const PO*>(stack_));
            append(built);
        }
    }

    // The container whose children are the top `o.want` slots of the stack.
    const PO* build(const Open& o) {
        std::vector<const PO*> items(o.want);
        for (size_t i = 0; i < o.want; ++i) items[i] = stack_->getAt(ctx_, listIndex(o.base + i));
        if (!o.map) {
            const proto::ProtoList* l = items.empty()
                ? ctx_->newList() : ctx_->newList(static_cast<unsigned>(items.size()), items.data());
            TransientPin pinList(ctx_, reinterpret_cast<const PO*>(l));
            return makeArrayInstance(rt_, ctx_, l);
        }
        if (!ordered_) {
            const PO* d = makeEmptyDictionary(rt_, ctx_);
            TransientPin pinDict(ctx_, d);
            for (size_t i = 0; i < items.size(); i += 2) dictionaryPut(ctx_, d, items[i], items[i + 1]);
            return d;
        }
        const proto::ProtoList* assocs = ctx_->newList();
        TransientPin pinAssocs(ctx_, reinterpret_cast<const PO*>(assocs));
        for (size_t i = 0; i < items.size(); i += 2) {
            const PO* assoc = makeAssociationValue(rt_, ctx_, items[i], items[i + 1]);
            TransientPin pinAssoc(ctx_, assoc);
            assocs = assocs->appendLast(ctx_, assoc);
            pinAssocs.reset(reinterpret_cast<const PO*>(assocs));
        }
        const PO* array = makeArrayInstance(rt_, ctx_, assocs);
        TransientPin pinArray(ctx_, array);
        bool understood = false;
        const PO* m = sendDynamic(rt_, ctx_, mapClass_,
                                  proto::ProtoString::createSymbol(ctx_, "__fromAssociations:"), &array, 1,
                                  &understood);
        if (!understood) throw std::runtime_error("MessagePackMap class does not understand __fromAssociations:");
        return m;
    }

    STRuntime& rt_;
    proto::ProtoContext* ctx_;
    bool ordered_;
    const PO* mapClass_;
    const PO* extClass_;
    const proto::ProtoList* stack_;
    TransientPin pin_;
    std::vector<Open> open_;
};

// aClass __msgpackDecode: bytes strict: b ordered: b map: aClass ext: aClass first: b
//   -> the object, or (first) an Association of the object and the bytes read.
PRIM(prim_MsgpackDecode) {
    ARGS(6, "__msgpackDecode:strict:ordered:map:ext:first:");
    const std::string_view input = bytesView(ctx, a[0]);
    const bool strict = booleanArg(a[1], "MessagePack decode");
    const bool ordered = booleanArg(a[2], "MessagePack decode");
    const bool first = booleanArg(a[5], "MessagePack decode");
    protoio::msgpack::Limits limits;
    limits.strictMapKeys = strict;
    size_t used = 0;
    Decoder dec(rt, ctx, ordered, a[3], a[4]);
    try {
        used = protoio::msgpack::read(input, dec, limits);
    } catch (const protoio::Error& e) {
        raiseProtoioError(e, "MessagePack decode");
    }
    if (!first && used != input.size())
        throw std::runtime_error("MessagePack decode: " + std::to_string(input.size() - used)
                                 + " trailing byte(s) after the first object (use decodeFirst: to allow them)");
    const PO* value = dec.result();
    if (!first) return value;
    TransientPin pinValue(ctx, value);
    return makeAssociationValue(rt, ctx, value, ctx->fromLong(static_cast<long long>(used)));
}

#undef ARGS
#undef PRIM

} // namespace

void installMessagePackPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    const proto::ProtoObject* o = rt.bootstrap().objectProto;
    bindPrimitive(rt, o, "__msgpackEncode:map:ext:", reg.registerPrim(prim_MsgpackEncode));
    bindPrimitive(rt, o, "__msgpackDecode:strict:ordered:map:ext:first:", reg.registerPrim(prim_MsgpackDecode));
}

} // namespace protoST
