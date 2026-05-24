#include "BytecodeModule.h"
#include "protoCore.h"
#include <algorithm>
#include <stdexcept>
namespace protoST {

// Lazy cache. The engine's pushFrame called this on every method call;
// fib(25) does ~150K calls, each one rescanning its ~30-byte method to
// recompute the (static) maxSlot. Perf-traced at 3.58 % of fib CPU. The
// bytecode is immutable post-compilation, so the answer is computed once
// and reused. Different `argc` values would only differ in the max(argc,
// maxSlot+1) term — the cache validates argc and rescans if it differs,
// which in practice never fires (argc == m.argCount() always).
unsigned int BytecodeModule::cachedLocalCount(unsigned int argc) const {
    if (cachedLocalCount_ != 0xFFFFFFFFu && cachedLocalCountArgc_ == argc) {
        return cachedLocalCount_;
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
    cachedLocalCount_     = needed;
    cachedLocalCountArgc_ = argc;
    return needed;
}

const proto::ProtoString* BytecodeModule::constSym(proto::ProtoContext* ctx,
                                                   size_t i) const {
    if (symCache_.size() != consts_.size())
        symCache_.assign(consts_.size(), nullptr);
    const proto::ProtoString*& slot = symCache_[i];
    if (!slot)
        slot = proto::ProtoString::createSymbol(ctx, consts_[i].sval.c_str());
    return slot;
}

const proto::ProtoString* BytecodeModule::ivSymbol(proto::ProtoContext* ctx,
                                                   size_t i) const {
    if (ivSymCache_.size() != consts_.size())
        ivSymCache_.assign(consts_.size(), nullptr);
    const proto::ProtoString*& slot = ivSymCache_[i];
    if (!slot) {
        std::string mangled = "_iv_";
        mangled += consts_[i].sval;
        slot = proto::ProtoString::createSymbol(ctx, mangled.c_str());
    }
    return slot;
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
    consts_.push_back(Const{ConstKind::Integer, v, 0.0, {}, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addFloat(double v) {
    consts_.push_back(Const{ConstKind::Float, 0, v, {}, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addString(const std::string& s) {
    consts_.push_back(Const{ConstKind::String, 0, 0.0, s, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::internSymbol(const std::string& s) {
    auto it = symbolIndex_.find(s);
    if (it != symbolIndex_.end()) return it->second;
    consts_.push_back(Const{ConstKind::Symbol, 0, 0.0, s, 0});
    size_t idx = consts_.size() - 1;
    symbolIndex_[s] = idx;
    return idx;
}
size_t BytecodeModule::addChar(const std::string& utf8) {
    consts_.push_back(Const{ConstKind::Char, 0, 0.0, utf8, 0});
    return consts_.size() - 1;
}
size_t BytecodeModule::addBlockRef(size_t blockIndex) {
    consts_.push_back(Const{ConstKind::BlockRef, 0, 0.0, {}, blockIndex});
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
