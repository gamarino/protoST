#include "BytecodeModule.h"
namespace protoST {

void BytecodeModule::emit(Op op, uint8_t arg, int line) {
    bytes_.push_back(static_cast<uint8_t>(op));
    bytes_.push_back(arg);
    // One line entry per 2-byte instruction; keeps instrLines_.size()
    // exactly equal to bytes_.size() / 2.
    instrLines_.push_back(line);
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
