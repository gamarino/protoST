#pragma once
#include "Opcodes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto { class ProtoContext; class ProtoString; }

namespace protoST {

class BytecodeModule {
public:
    enum class ConstKind : uint8_t {
        Integer, Float, String, Symbol, Char, BlockRef, NilK, TrueK, FalseK,
        // Resolves to `Bootstrap::unsetMarker` at runtime. Emitted by the
        // call-form method prologue so the dispatcher's "unset" sentinel can
        // be compared against named-arg slots without going through the
        // SymbolTable. Carries no payload — the runtime substitutes the
        // bootstrap singleton when this constant is pushed.
        UnsetMarker,
    };

    struct Const {
        ConstKind kind;
        long long ival = 0;
        double    fval = 0.0;
        std::string sval;
        size_t      blockIndex = 0;  // for BlockRef
    };

    BytecodeModule() = default;
    ~BytecodeModule();
    BytecodeModule(const BytecodeModule&) = delete;
    BytecodeModule& operator=(const BytecodeModule&) = delete;

    // emission
    // `line` is the 1-based source line the instruction originates from;
    // 0 means "unknown". One line entry is recorded per emitted 2-byte
    // instruction word. With wide operands an instruction may span several
    // words (one or more EXTEND prefixes plus the real opcode word); each
    // word records the same line and its own byte-start, so the line map
    // stays correct for variable-width instructions (BL-2).
    void emit(Op op, uint8_t arg, int line = 0);

    // BL-2: emit `op` with a possibly-wide operand. When `arg` exceeds 255 an
    // EXTEND prefix word is emitted carrying the high byte(s); the engine
    // latches those bits and combines them with the real word's low byte.
    // Operands up to 2^24-1 are supported (two EXTEND prefixes at most).
    void emitWide(Op op, unsigned int arg, int line = 0);

    // patching (for jumps) — patchArg rewrites only the arg byte of an
    // already-emitted instruction word, so the line map is unaffected.
    size_t  pos() const { return bytes_.size(); }
    void    patchArg(size_t bytePos, uint8_t arg) { bytes_[bytePos + 1] = arg; }

    // constants
    size_t  addInteger(long long v);
    size_t  addFloat(double v);
    size_t  addString(const std::string& s);
    size_t  internSymbol(const std::string& s);  // de-duplicated
    size_t  addChar(const std::string& utf8);
    size_t  addBlockRef(size_t blockIndex);
    // Push-an-unset-sentinel constant. Idempotent: adding it twice is
    // allowed but the cache only needs a single live entry; we still
    // append, leaving deduplication to the compiler if it cares.
    size_t  addUnsetMarker();

    // accessors
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    long long           constInteger(size_t i) const { return consts_[i].ival; }
    double              constFloat(size_t i)   const { return consts_[i].fval; }
    const std::string&  constString(size_t i)  const { return consts_[i].sval; }
    const std::string&  constSymbol(size_t i)  const { return consts_[i].sval; }
    ConstKind           constKind(size_t i)    const { return consts_[i].kind; }

    // ---------------------------------------------------------------------
    // Per-constant runtime caches (D25)
    //
    // A module is immutable once it executes, but the engine resolves some
    // constants lazily and caches the result on the module: the interned
    // symbol (constSym), the mangled instance-variable key (ivSymbol) and the
    // parsed call-form descriptor (callDescriptor). Several worker threads
    // can execute a module for the first time at the same moment, so these
    // caches must be safe for concurrent first use:
    //
    //   * All three live in one table with one entry per constant, allocated
    //     on first use at the module's constant count and published with a
    //     compare-and-swap. A published table is never resized or freed
    //     while the module lives, so a reader can never see a freed buffer.
    //     (Only a hand-built module that gains constants after it first ran
    //     needs a larger table; that table is published in front of the old
    //     one, and the old one is kept until the module is destroyed.)
    //   * Every entry is an atomic. Symbol entries are filled with a plain
    //     store: racing threads store the same pointer, because interned
    //     symbols are unique per ProtoSpace and inline symbols are
    //     bit-identical. Symbols are perpetual (never collected), so caching
    //     the raw pointer needs no GC root.
    //   * A call descriptor is parsed off to the side and published with a
    //     compare-and-swap from null; the loser drops its copy and uses the
    //     winner's. A published descriptor is immutable.
    //
    // The fast paths below are one table load, a bounds check and one slot
    // load.
    // ---------------------------------------------------------------------

    // Interned symbol for constant-pool entry `i`, cached. The opcodes that
    // resolve a name — SEND_* (the selector), PUSH/STORE_GLOBAL,
    // PUSH/STORE_CAPTURED — run constantly; calling createSymbol (a SymbolTable
    // hash over a freshly parsed rope) on every execution was measured as a
    // dominant message-path cost. The symbol for a given constant never
    // changes, so it is interned once and cached.
    const proto::ProtoString* constSym(proto::ProtoContext* ctx, size_t i) const {
        if (const ConstCache* c = constCache_.load(std::memory_order_acquire);
            c && i < c->size) {
            if (const proto::ProtoString* s =
                    c->entries[i].sym.load(std::memory_order_acquire))
                return s;
        }
        return constSymSlow(ctx, i);
    }

    // Cached interned "_iv_<name>" symbol for constant-pool entry `i` — the
    // mangled instance-variable storage key (PUSH_INSTVAR / STORE_INSTVAR).
    // Same rationale as constSym; a separate entry because the key carries
    // the "_iv_" prefix.
    const proto::ProtoString* ivSymbol(proto::ProtoContext* ctx, size_t i) const {
        if (const ConstCache* c = constCache_.load(std::memory_order_acquire);
            c && i < c->size) {
            if (const proto::ProtoString* s =
                    c->entries[i].ivSym.load(std::memory_order_acquire))
                return s;
        }
        return ivSymbolSlow(ctx, i);
    }
    size_t              constBlockRef(size_t i)const { return consts_[i].blockIndex; }

    // sub-modules
    size_t              addBlockModule(std::unique_ptr<BytecodeModule> b);
    const BytecodeModule& block(size_t i) const { return *blocks_[i]; }
    size_t              numBlocks() const { return blocks_.size(); }

    // block metadata
    void setArgCount(int n) { argCount_ = n; }
    int  argCount() const   { return argCount_; }

    // BL-1: the name of the class whose method body this module compiles.
    // Empty for the top-level module and for plain blocks. The engine uses
    // it to resolve `super` sends: a `super foo` inside a method defined on
    // class C must start method lookup at C's parent, not at the receiver's
    // own class. Set by the compiler's MethodDecl emission.
    void setDefiningClass(const std::string& s) { definingClass_ = s; }
    const std::string& definingClass() const { return definingClass_; }

    // Call-form method signature. Populated by the compiler when this module
    // is the body of a `Class >> name(pos, named=default)` declaration. The
    // dispatcher reads these to adapt a SEND_CALL site to the declared
    // shape (arity-check + sentinel-fill for omitted named args).
    //
    //   callPosArity        -- declared positional arity (number of params
    //                          before the named block; zero is legal).
    //   callNamedKeys       -- declared named-arg keys, sorted alphabetically.
    //                          Length defines the named arity.
    //   isCallForm          -- true exactly when this module is a call-form
    //                          method body. False for plain MethodDecl,
    //                          block bodies, and module bodies — they keep
    //                          today's keyword/unary dispatch.
    void setCallFormSignature(int posArity,
                              std::vector<std::string> sortedNamedKeys) {
        isCallForm_     = true;
        callPosArity_   = posArity;
        callNamedKeys_  = std::move(sortedNamedKeys);
    }
    bool isCallForm()        const { return isCallForm_; }
    int  callPosArity()      const { return callPosArity_; }
    const std::vector<std::string>& callNamedKeys() const { return callNamedKeys_; }

    // Parsed form of a SEND_CALL operand. The operand names a Symbol constant
    // whose text follows the mangling `<name>#<nPos>[#<key1>#<key2>...]`. The
    // engine parses it once into (interned name, nPos, sorted-key vector) and
    // publishes the result on the module (see the cache notes above), so later
    // passes through the same send site skip the string split entirely.
    // Immutable once published.
    struct CallDescriptor {
        const proto::ProtoString* name = nullptr;
        int nPos = 0;
        std::vector<const proto::ProtoString*> sortedKeys;
        // Original mangle text, used to enrich `doesNotUnderstand:` messages.
        std::string mangled;
    };
    // The published descriptor for constant `constIdx`, or nullptr when no
    // thread has published one yet.
    const CallDescriptor* callDescriptor(size_t constIdx) const {
        const ConstCache* c = constCache_.load(std::memory_order_acquire);
        return (c && constIdx < c->size)
                   ? c->entries[constIdx].callDesc.load(std::memory_order_acquire)
                   : nullptr;
    }
    // Publishes `desc` for constant `constIdx` unless a descriptor is already
    // published, and returns the published descriptor: `desc` itself, or the
    // one that won the race (in which case `desc` is destroyed). Never null.
    // The module owns published descriptors.
    const CallDescriptor* publishCallDescriptor(
        size_t constIdx, std::unique_ptr<CallDescriptor> desc) const;

    // F8-1 / BL-2: source-line mapping. instrLines_ holds one line entry per
    // emitted 2-byte word; instrStartPc_ holds the byte offset of each word.
    // For a fixed-width module (no EXTEND prefixes) instrStartPc_[i] == i*2,
    // so pc/2 indexing still holds; once wide operands appear the byte offset
    // is no longer i*2 and lineForPc must look pc up by its real start.
    //
    // The F8-1 API is unchanged: lineForPc(pc) -> line, firstPcForLine(line)
    // -> lowest breakable pc. Only the indexing scheme behind them changed.
    int lineForPc(size_t pc) const {
        // Fast path: an instruction word starts exactly at `pc`.
        for (size_t i = 0; i < instrStartPc_.size(); ++i) {
            if (instrStartPc_[i] == pc) return instrLines_[i];
        }
        // Slow path: `pc` lands inside a word (defensive — callers always
        // pass instruction-aligned pcs). Return the line of the word that
        // contains it.
        int last = 0;
        for (size_t i = 0; i < instrStartPc_.size(); ++i) {
            if (instrStartPc_[i] <= pc) last = instrLines_[i];
            else break;
        }
        return (pc < bytes_.size()) ? last : 0;
    }
    // Lowest pc whose instruction maps to `line`. Returns SIZE_MAX when no
    // instruction maps to that line (used for breakpoint resolution).
    size_t firstPcForLine(int line) const {
        for (size_t i = 0; i < instrLines_.size(); ++i) {
            if (instrLines_[i] == line) return instrStartPc_[i];
        }
        return SIZE_MAX;
    }
    const std::vector<int>& instrLines() const { return instrLines_; }
    // BL-2: byte offset of each instruction word, parallel to instrLines_.
    const std::vector<size_t>& instrStartPc() const { return instrStartPc_; }

    // F8-4: local-slot names. The compiler addresses locals by slot index;
    // the bytecode itself carries no names. To let the DAP Variables panel
    // show real identifiers (`count`, `x`, `self`, ...) the compiler records
    // the declared name for each slot here, parallel to the slot index. Slot
    // i's name is localNames_[i]; an out-of-range slot (or a name never
    // recorded) yields an empty string and the caller falls back to "slot N".
    void setLocalNames(std::vector<std::string> names) {
        localNames_ = std::move(names);
    }
    const std::string& localName(size_t slot) const {
        static const std::string kEmpty;
        return (slot < localNames_.size()) ? localNames_[slot] : kEmpty;
    }
    const std::vector<std::string>& localNames() const { return localNames_; }

    // F8-4: an optional human label for this module (method selector, block,
    // or "<module>"). Used as the stack-frame name in the DAP call stack.
    const std::string& debugName() const { return debugName_; }
    void setDebugName(const std::string& s) { debugName_ = s; }

    // F8-1: source file path/name this module was compiled from. Set
    // recursively so that already-attached sub-blocks inherit the name.
    const std::string& sourceName() const { return sourceName_; }
    void setSourceName(const std::string& s) {
        sourceName_ = s;
        for (auto& b : blocks_) {
            if (b) b->setSourceName(s);
        }
    }

private:
    // One entry per constant; see the cache notes above.
    struct ConstCacheEntry {
        std::atomic<const proto::ProtoString*> sym{nullptr};
        std::atomic<const proto::ProtoString*> ivSym{nullptr};
        std::atomic<const CallDescriptor*>     callDesc{nullptr};
    };
    // A fixed-size table of entries. Owns the descriptors published into it
    // and the older (smaller) table it replaced, if any.
    struct ConstCache {
        ConstCache(size_t n, ConstCache* olderTable)
            : size(n), entries(new ConstCacheEntry[n]), older(olderTable) {}
        ~ConstCache();
        ConstCache(const ConstCache&) = delete;
        ConstCache& operator=(const ConstCache&) = delete;

        const size_t                       size;
        std::unique_ptr<ConstCacheEntry[]> entries;
        ConstCache*                        older;
    };

    // Returns the published table, publishing one first if there is none or
    // the published one does not cover constant `i`.
    const ConstCache& constCacheFor(size_t i) const;
    const proto::ProtoString* constSymSlow(proto::ProtoContext* ctx, size_t i) const;
    const proto::ProtoString* ivSymbolSlow(proto::ProtoContext* ctx, size_t i) const;

    std::vector<uint8_t>                bytes_;
    std::vector<int>                    instrLines_;   // F8-1: line per word
    std::vector<size_t>                 instrStartPc_; // BL-2: byte start per word
    std::string                         sourceName_;  // F8-1: source file path
    std::vector<std::string>            localNames_;  // F8-4: name per local slot
    std::string                         debugName_;   // F8-4: human label
    std::vector<Const>                  consts_;
    // Per-constant runtime caches (constSym / ivSymbol / callDescriptor).
    // Null until first use. Holds perennial interned ProtoStrings, valid for
    // the runtime's ProtoSpace (one runtime per process).
    mutable std::atomic<ConstCache*>    constCache_{nullptr};
    std::unordered_map<std::string, size_t> symbolIndex_;
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    int argCount_ = 0;
    std::string definingClass_;   // BL-1: class owning this method body
    // Call-form signature (populated by the compiler for call-form bodies).
    bool isCallForm_   = false;
    int  callPosArity_ = 0;
    std::vector<std::string> callNamedKeys_;  // sorted alphabetically

public:
    // 2026-05-24 perf: localCount is a static property of the bytecode
    // (= max PUSH_LOCAL/STORE_LOCAL slot index + 1). The engine used to
    // recompute it from a full bytecode scan on every pushFrame —
    // measured at 3.58 % of fib CPU because fib(25) does ~150K method
    // calls, each scanning a ~30-byte method. Lazy-cache it here on
    // first request; bytecode is immutable after compilation so the
    // cached value never invalidates.
    unsigned int cachedLocalCount(unsigned int argc) const;

private:
    // `(argc << 32) | localCount`, or kNoLocalCount when not yet computed.
    // argc is folded into the cache key because `computeLocalCount` returns
    // max(argc, maxSlot+1) — different argc values for the same module body
    // would yield different counts (in practice argc is always the module's
    // declared argCount so this is just a safety check). One atomic word, so
    // a concurrent first call never reads a count paired with another argc.
    static constexpr uint64_t kNoLocalCount = UINT64_MAX;
    mutable std::atomic<uint64_t> localCountCache_{kNoLocalCount};
};

} // namespace protoST
