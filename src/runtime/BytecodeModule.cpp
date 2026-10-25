#include "BytecodeModule.h"
#include "protoCore.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <stdexcept>
namespace protoST {

BytecodeModule::~BytecodeModule() {
    delete constCache_.load(std::memory_order_acquire);
}

BytecodeModule::ConstCache::~ConstCache() {
    for (size_t i = 0; i < size; ++i)
        delete entries[i].callDesc.load(std::memory_order_acquire);
    delete older;
}

// Lazy cache. The engine's pushFrame called this on every method call;
// fib(25) does ~150K calls, each one rescanning its ~30-byte method to
// recompute the (static) maxSlot. Perf-traced at 3.58 % of fib CPU. The
// bytecode is immutable post-compilation, so the answer is computed once
// and reused. Different `argc` values would only differ in the max(argc,
// maxSlot+1) term — the cache validates argc and rescans if it differs,
// which in practice never fires (argc == m.argCount() always).
unsigned int BytecodeModule::cachedLocalCount(unsigned int argc) const {
    const uint64_t cached = localCountCache_.load(std::memory_order_acquire);
    if (cached != kNoLocalCount && static_cast<unsigned int>(cached >> 32) == argc) {
        return static_cast<unsigned int>(cached);
    }
    unsigned int maxSlot = 0;
    bool sawSlot = false;
    // Decode EXTEND prefixes exactly as the engine does (BL-2).
    for (std::size_t pc = 0; pc + 1 < bytes_.size(); ) {
        Op op  = static_cast<Op>(bytes_[pc]);
        unsigned int arg = bytes_[pc + 1];
        pc += kInstrSize;
        while (op == Op::EXTEND && pc + 1 < bytes_.size()) {
            op  = static_cast<Op>(bytes_[pc]);
            arg = (arg << 8) | bytes_[pc + 1];
            pc += kInstrSize;
        }
        if (op == Op::PUSH_LOCAL || op == Op::STORE_LOCAL) {
            if (!sawSlot || arg > maxSlot) { maxSlot = arg; sawSlot = true; }
        }
    }
    unsigned int needed = sawSlot ? (maxSlot + 1) : 0;
    needed = std::max(needed, argc);
    // The engine also enforces a kMinLocals floor (kSelfSlot + 1 + a few
    // header slots), but that constant lives in ExecutionEngine.cpp. We
    // return the raw computed need here; the caller folds in kMinLocals.
    // Concurrent first calls compute and store the same word.
    localCountCache_.store((static_cast<uint64_t>(argc) << 32) | needed,
                           std::memory_order_release);
    return needed;
}

unsigned int BytecodeModule::sendArgCount(Op op, const std::string& sel) {
    if (op == Op::SEND_UNARY) return 0;
    if (op == Op::SEND_BINARY) return 1;
    if (op == Op::SEND_CALL) {
        const std::size_t h1 = sel.find('#');
        if (h1 == std::string::npos) return 0;
        unsigned int n = static_cast<unsigned int>(std::strtoul(sel.c_str() + h1 + 1, nullptr, 10));
        for (std::size_t i = sel.find('#', h1 + 1); i != std::string::npos;
             i = sel.find('#', i + 1))
            ++n;
        return n;
    }
    unsigned int colons = 0;
    for (char c : sel) if (c == ':') ++colons;
    if (op == Op::SEND_SUPER && colons == 0 && !sel.empty()) {
        const unsigned char c0 = static_cast<unsigned char>(sel[0]);
        if (!std::isalpha(c0) && c0 != '_') return 1;   // binary
    }
    return colons;
}

unsigned int BytecodeModule::cachedMaxOperandDepth() const {
    const int cached = maxOperandDepthCache_.load(std::memory_order_acquire);
    if (cached >= 0) return static_cast<unsigned int>(cached);
    // One pass in code order. The compiler emits structured code: every jump
    // into a point comes either from before it (a forward jump, which
    // records its depth here) or is a loop's back edge (whose target was
    // already visited with the same depth). After an unconditional transfer
    // the next instruction is reached only by a forward jump, so its depth is
    // the recorded one; code no jump reaches (after a `^` in an inlined
    // block) keeps the last depth, an overestimate at worst.
    std::unordered_map<std::size_t, long> atTarget;
    long depth = 0, maxDepth = 0;
    bool reachable = true;
    auto pushes = [&](long n) {
        depth += n;
        if (depth < 0) depth = 0;   // a hand-built module may underflow
        if (depth > maxDepth) maxDepth = depth;
    };
    for (std::size_t pc = 0; pc + 1 < bytes_.size(); ) {
        const std::size_t start = pc;
        Op op  = static_cast<Op>(bytes_[pc]);
        unsigned int arg = bytes_[pc + 1];
        pc += kInstrSize;
        while (op == Op::EXTEND && pc + 1 < bytes_.size()) {
            op  = static_cast<Op>(bytes_[pc]);
            arg = (arg << 8) | bytes_[pc + 1];
            pc += kInstrSize;
        }
        if (auto it = atTarget.find(start); it != atTarget.end()) {
            depth = reachable ? std::max(depth, it->second) : it->second;
            reachable = true;
        }
        auto jumpTo = [&](std::size_t target, long d) {
            long& t = atTarget[target];
            if (d > t) t = d;
        };
        switch (op) {
            case Op::PUSH_CONST: case Op::PUSH_LOCAL: case Op::DUP:
            case Op::PUSH_SELF: case Op::PUSH_NIL: case Op::PUSH_TRUE:
            case Op::PUSH_FALSE: case Op::PUSH_BLOCK: case Op::PUSH_CAPTURED:
            case Op::PUSH_GLOBAL: case Op::PUSH_INSTVAR: case Op::PUSH_OWN_INSTVAR:
                pushes(1);
                break;
            case Op::STORE_LOCAL: case Op::POP: case Op::STORE_CAPTURED:
            case Op::STORE_GLOBAL: case Op::STORE_INSTVAR: case Op::STORE_CLASSVAR:
            case Op::DEFINE_CAPTURED:
                pushes(-1);
                break;
            case Op::SEND_UNARY: case Op::SEND_BINARY: case Op::SEND_KEYWORD:
            case Op::SEND_SUPER: case Op::SEND_CALL:
                if (arg < consts_.size())
                    pushes(-static_cast<long>(sendArgCount(op, consts_[arg].sval)));
                break;
            case Op::BIN_INT_ADD: case Op::BIN_INT_SUB: case Op::BIN_INT_LE:
            case Op::BIN_INT_LT: case Op::BIN_INT_GE: case Op::BIN_INT_GT:
            case Op::BIN_INT_EQ:
                pushes(-1);
                break;
            case Op::MAKE_ARRAY:
                pushes(1 - static_cast<long>(arg));
                break;
            case Op::IVAR_GROUP_END:
                if (arg < groupEntries_.size() && groupEntries_[arg].count > 0)
                    pushes(1 - static_cast<long>(groupEntries_[arg].count));
                break;
            case Op::JUMP_IF_TRUE: case Op::JUMP_IF_FALSE:
                pushes(-1);
                jumpTo(pc + static_cast<std::size_t>(arg) * kInstrSize, depth);
                break;
            case Op::JUMP:
                jumpTo(pc + static_cast<std::size_t>(arg) * kInstrSize, depth);
                reachable = false;
                break;
            case Op::JUMP_BACK: case Op::RETURN: case Op::RETURN_TOP:
                reachable = false;
                break;
            default:   // ASSERT_BOOL_OR_DNU, MAKE_CAPTURED, IVAR_GROUP: no change
                break;
        }
    }
    const int result = static_cast<int>(std::min<long>(maxDepth, INT32_MAX));
    maxOperandDepthCache_.store(result, std::memory_order_release);
    return static_cast<unsigned int>(result);
}

void BytecodeModule::insertExtendPrefixes(size_t bytePos, unsigned int count) {
    if (count == 0) return;
    if (bytePos >= bytes_.size() || bytePos % kInstrSize != 0)
        throw std::logic_error("insertExtendPrefixes: not an instruction start");
    const size_t added = static_cast<size_t>(count) * kInstrSize;
    std::vector<uint8_t> prefix;
    for (unsigned int i = 0; i < count; ++i) {
        prefix.push_back(static_cast<uint8_t>(Op::EXTEND));
        prefix.push_back(0);
    }
    bytes_.insert(bytes_.begin() + static_cast<std::ptrdiff_t>(bytePos),
                  prefix.begin(), prefix.end());
    // Word tables: one entry per word, word i at byte 2*i (every word is
    // two bytes, so the index of the word at bytePos is bytePos / 2).
    const size_t word = bytePos / kInstrSize;
    const int line = word < instrLines_.size() ? instrLines_[word] : 0;
    instrLines_.insert(instrLines_.begin() + static_cast<std::ptrdiff_t>(word), count, line);
    for (size_t& start : instrStartPc_)
        if (start >= bytePos) start += added;
    std::vector<size_t> newStarts;
    for (unsigned int i = 0; i < count; ++i) newStarts.push_back(bytePos + i * kInstrSize);
    instrStartPc_.insert(instrStartPc_.begin() + static_cast<std::ptrdiff_t>(word),
                         newStarts.begin(), newStarts.end());
    for (ScopedLocal& s : scopedLocals_) {
        if (s.fromPc > bytePos) s.fromPc += added;
        if (s.toPc   > bytePos) s.toPc   += added;
    }
}

void BytecodeModule::patchWideArg(size_t bytePos, unsigned int prefixes, unsigned int arg) {
    if (extendPrefixesFor(arg) > prefixes)
        throw std::logic_error("patchWideArg: operand does not fit");
    for (unsigned int i = 0; i < prefixes; ++i) {
        const unsigned int shift = 8u * (prefixes - i);
        bytes_[bytePos + i * kInstrSize + 1] = static_cast<uint8_t>((arg >> shift) & 0xFF);
    }
    bytes_[bytePos + prefixes * kInstrSize + 1] = static_cast<uint8_t>(arg & 0xFF);
}

const BytecodeModule::ConstCache&
BytecodeModule::constCacheFor(size_t i) const {
    if (i >= consts_.size())
        throw std::out_of_range("BytecodeModule: constant index out of range");
    ConstCache* cur = constCache_.load(std::memory_order_acquire);
    while (!cur || i >= cur->size) {
        // First use, or a hand-built module gained constants after its table
        // was published. Publish a table covering every constant in front of
        // the current one; the current table is never freed while the module
        // lives, so readers still holding it stay valid.
        auto* fresh = new ConstCache(consts_.size(), cur);
        if (constCache_.compare_exchange_strong(cur, fresh,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
            return *fresh;
        }
        // Lost the race: `cur` now holds the winner's table. Ours was never
        // visible to any other thread; detach the older table it borrowed
        // before destroying it.
        fresh->older = nullptr;
        delete fresh;
    }
    return *cur;
}

const proto::ProtoString* BytecodeModule::constSymSlow(proto::ProtoContext* ctx,
                                                       size_t i) const {
    const ConstCache& c = constCacheFor(i);
    std::atomic<const proto::ProtoString*>& slot = c.entries[i].sym;
    const proto::ProtoString* s = slot.load(std::memory_order_acquire);
    if (!s) {
        s = proto::ProtoString::createSymbol(ctx, consts_[i].sval.c_str());
        // Racing first uses intern the same name and store the same pointer.
        slot.store(s, std::memory_order_release);
    }
    return s;
}

const proto::ProtoString* BytecodeModule::ivSymbolSlow(proto::ProtoContext* ctx,
                                                       size_t i) const {
    const ConstCache& c = constCacheFor(i);
    std::atomic<const proto::ProtoString*>& slot = c.entries[i].ivSym;
    const proto::ProtoString* s = slot.load(std::memory_order_acquire);
    if (!s) {
        std::string mangled = "_iv_";
        mangled += consts_[i].sval;
        s = proto::ProtoString::createSymbol(ctx, mangled.c_str());
        // Racing first uses intern the same name and store the same pointer.
        slot.store(s, std::memory_order_release);
    }
    return s;
}

const BytecodeModule::CallDescriptor*
BytecodeModule::publishCallDescriptor(size_t constIdx,
                                      std::unique_ptr<CallDescriptor> desc) const {
    const ConstCache& c = constCacheFor(constIdx);
    const CallDescriptor* expected = nullptr;
    if (c.entries[constIdx].callDesc.compare_exchange_strong(
            expected, desc.get(),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return desc.release();   // the table owns it now
    }
    return expected;             // an earlier publisher won; `desc` is dropped
}

void BytecodeModule::emit(Op op, uint8_t arg, int line) {
    // Record the byte offset of this word BEFORE appending its two bytes, so
    // instrStartPc_[i] is the real start of word i (correct even when the
    // module mixes 2-byte and EXTEND-prefixed wider instructions — BL-2).
    instrStartPc_.push_back(bytes_.size());
    bytes_.push_back(static_cast<uint8_t>(op));
    bytes_.push_back(arg);
    // One line entry per 2-byte word; instrLines_.size() stays exactly equal
    // to instrStartPc_.size() and to bytes_.size() / 2.
    instrLines_.push_back(line);
}

void BytecodeModule::emitWide(Op op, unsigned int arg, int line) {
    // BL-2: encode an operand wider than 8 bits with one or more EXTEND
    // prefix words (Python EXTENDED_ARG style). EXTEND carries the next-most-
    // significant byte; the engine latches it and shifts on each EXTEND seen.
    // The real opcode word always carries the low 8 bits.
    if (arg > 0x00FFFFFFu)
        throw std::runtime_error("bytecode operand exceeds 24-bit limit");
    if (arg > 0xFFFFu) {
        emit(Op::EXTEND, static_cast<uint8_t>((arg >> 16) & 0xFF), line);
        emit(Op::EXTEND, static_cast<uint8_t>((arg >>  8) & 0xFF), line);
    } else if (arg > 0xFFu) {
        emit(Op::EXTEND, static_cast<uint8_t>((arg >>  8) & 0xFF), line);
    }
    emit(op, static_cast<uint8_t>(arg & 0xFF), line);
}

size_t BytecodeModule::addInteger(long long v) {
    consts_.push_back(Const{ConstKind::Integer, v, 0.0, {}});
    return consts_.size() - 1;
}
size_t BytecodeModule::addLargeInteger(const std::string& digits, int radix) {
    consts_.push_back(Const{ConstKind::LargeInteger, radix, 0.0, digits});
    return consts_.size() - 1;
}
size_t BytecodeModule::addFloat(double v) {
    consts_.push_back(Const{ConstKind::Float, 0, v, {}});
    return consts_.size() - 1;
}
size_t BytecodeModule::addString(const std::string& s) {
    consts_.push_back(Const{ConstKind::String, 0, 0.0, s});
    return consts_.size() - 1;
}
size_t BytecodeModule::internSymbol(const std::string& s) {
    auto it = symbolIndex_.find(s);
    if (it != symbolIndex_.end()) return it->second;
    consts_.push_back(Const{ConstKind::Symbol, 0, 0.0, s});
    size_t idx = consts_.size() - 1;
    symbolIndex_[s] = idx;
    return idx;
}
size_t BytecodeModule::addChar(const std::string& utf8) {
    consts_.push_back(Const{ConstKind::Char, 0, 0.0, utf8});
    return consts_.size() - 1;
}
size_t BytecodeModule::addUnsetMarker() {
    // No payload — the engine substitutes Bootstrap::unsetMarker when this
    // constant is pushed via PUSH_CONST.
    consts_.push_back(Const{ConstKind::UnsetMarker, 0, 0.0, {}});
    return consts_.size() - 1;
}
size_t BytecodeModule::addBlockModule(std::unique_ptr<BytecodeModule> b) {
    // F8-1: a sub-block inherits the parent's source name. If the parent's
    // sourceName_ is set later, setSourceName re-stamps all sub-blocks too.
    if (b) b->sourceName_ = sourceName_;
    blocks_.push_back(std::move(b));
    return blocks_.size() - 1;
}

} // namespace protoST
