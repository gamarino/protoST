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

    // BL-1: the name of the class whose method body this module compiles.
    // Empty for the top-level module and for plain blocks. The engine uses
    // it to resolve `super` sends: a `super foo` inside a method defined on
    // class C must start method lookup at C's parent, not at the receiver's
    // own class. Set by the compiler's MethodDecl emission.
    void setDefiningClass(const std::string& s) { definingClass_ = s; }
    const std::string& definingClass() const { return definingClass_; }

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
    std::vector<uint8_t>                bytes_;
    std::vector<int>                    instrLines_;   // F8-1: line per word
    std::vector<size_t>                 instrStartPc_; // BL-2: byte start per word
    std::string                         sourceName_;  // F8-1: source file path
    std::vector<std::string>            localNames_;  // F8-4: name per local slot
    std::string                         debugName_;   // F8-4: human label
    std::vector<Const>                  consts_;
    std::unordered_map<std::string, size_t> symbolIndex_;
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    int argCount_ = 0;
    std::string definingClass_;   // BL-1: class owning this method body
};

} // namespace protoST
