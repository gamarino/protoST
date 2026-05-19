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
    void emit(Op op, uint8_t arg);

    // patching (for jumps)
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

private:
    std::vector<uint8_t>                bytes_;
    std::vector<Const>                  consts_;
    std::unordered_map<std::string, size_t> symbolIndex_;
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    int argCount_ = 0;
};

} // namespace protoST
