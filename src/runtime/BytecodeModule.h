#pragma once
#include "Opcodes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoST {

class BytecodeModule {
public:
    enum class ConstKind : uint8_t { Integer, Float, String, Symbol, Char, BlockRef, NilK, TrueK, FalseK };

    struct Const {
        ConstKind kind;
        long long ival = 0;
        double    fval = 0.0;
        std::string sval;
        size_t      blockIndex = 0;  // for BlockRef
    };

    // emission
    // `line` is the 1-based source line the instruction originates from;
    // 0 means "unknown". One line entry is recorded per 2-byte instruction.
    void emit(Op op, uint8_t arg, int line = 0);

    // patching (for jumps) — patchArg rewrites only the arg byte of an
    // already-emitted instruction, so the line map is unaffected.
    size_t  pos() const { return bytes_.size(); }
    void    patchArg(size_t bytePos, uint8_t arg) { bytes_[bytePos + 1] = arg; }

    // constants
    size_t  addInteger(long long v);
    size_t  addFloat(double v);
    size_t  addString(const std::string& s);
    size_t  internSymbol(const std::string& s);  // de-duplicated
    size_t  addChar(const std::string& utf8);
    size_t  addBlockRef(size_t blockIndex);

    // accessors
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    long long           constInteger(size_t i) const { return consts_[i].ival; }
    double              constFloat(size_t i)   const { return consts_[i].fval; }
    const std::string&  constString(size_t i)  const { return consts_[i].sval; }
    const std::string&  constSymbol(size_t i)  const { return consts_[i].sval; }
    ConstKind           constKind(size_t i)    const { return consts_[i].kind; }
    size_t              constBlockRef(size_t i)const { return consts_[i].blockIndex; }

    // sub-modules
    size_t              addBlockModule(std::unique_ptr<BytecodeModule> b);
    const BytecodeModule& block(size_t i) const { return *blocks_[i]; }
    size_t              numBlocks() const { return blocks_.size(); }

    // block metadata
    void setArgCount(int n) { argCount_ = n; }
    int  argCount() const   { return argCount_; }

    // F8-1: source-line mapping. Each 2-byte instruction has one entry in
    // instrLines_; instruction index = pc / 2.
    int lineForPc(size_t pc) const {
        size_t i = pc / 2;
        return (i < instrLines_.size()) ? instrLines_[i] : 0;
    }
    // Lowest pc whose instruction maps to `line`. Returns SIZE_MAX when no
    // instruction maps to that line (used for breakpoint resolution).
    size_t firstPcForLine(int line) const {
        for (size_t i = 0; i < instrLines_.size(); ++i) {
            if (instrLines_[i] == line) return i * 2;
        }
        return SIZE_MAX;
    }
    const std::vector<int>& instrLines() const { return instrLines_; }

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
    std::vector<uint8_t>                bytes_;
    std::vector<int>                    instrLines_;  // F8-1: line per instruction
    std::string                         sourceName_;  // F8-1: source file path
    std::vector<Const>                  consts_;
    std::unordered_map<std::string, size_t> symbolIndex_;
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    int argCount_ = 0;
};

} // namespace protoST
