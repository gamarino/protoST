#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <shared_mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace protoST {

class BytecodeModule;

// The location breakpoints, shared by every thread of the debuggee: the
// adapter thread rewrites the table while script threads read it before each
// instruction, so every access takes the lock (shared for a lookup). An empty
// table answers without it.
class BreakpointTable {
public:
    bool isSet(const BytecodeModule* m, size_t pc) const;
    void add(const BytecodeModule* m, size_t pc);
    void remove(const BytecodeModule* m, size_t pc);
    void clear();
    // Replace the whole table in one step, so no thread sees it half built.
    void assign(const std::vector<std::pair<const BytecodeModule*, size_t>>& entries);
    size_t size() const;

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
    mutable std::shared_mutex             mu_;
    std::atomic<size_t>                   count_{0};
    std::unordered_set<Entry, EntryHash>  entries_;
};

} // namespace protoST
