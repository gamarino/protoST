#include "BytecodeModule.h"
#include <stdexcept>
namespace protoST {

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
