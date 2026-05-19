#pragma once
#include <cstddef>
#include <unordered_set>
#include <functional>

namespace protoST {

class BytecodeModule;

class BreakpointTable {
public:
    bool isSet(const BytecodeModule* m, size_t pc) const;
    void add(const BytecodeModule* m, size_t pc);
    void remove(const BytecodeModule* m, size_t pc);
    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        const BytecodeModule* module;
        size_t pc;
        bool operator==(const Entry& o) const noexcept { return module == o.module && pc == o.pc; }
    };
    struct EntryHash {
        size_t operator()(const Entry& e) const noexcept {
            return std::hash<const void*>()(e.module) ^ (std::hash<size_t>()(e.pc) << 1);
        }
    };
    std::unordered_set<Entry, EntryHash> entries_;
};

} // namespace protoST
