#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace protoST {

class BytecodeModule;

// What a line breakpoint does when it is reached, besides stopping: a
// condition (a Smalltalk expression evaluated in the frame; it stops only when
// it answers true), a hit condition over the number of hits whose condition
// passed, and a log message (it then logs instead of stopping). A plain
// breakpoint has none of them.
struct BreakpointSpec {
    enum class HitOp { None, Eq, Ge, Gt, Lt, Le, Mod };
    std::string       condition;
    HitOp             hitOp = HitOp::None;
    long              hitN = 0;
    bool              isLogpoint = false;
    std::string       logMessage;
    std::atomic<long> hits{0};

    bool plain() const { return condition.empty() && hitOp == HitOp::None && !isLogpoint; }
    // Count one hit (its condition passed) and answer whether the hit
    // condition selects it.
    bool countHit() {
        const long n = hits.fetch_add(1) + 1;
        switch (hitOp) {
            case HitOp::None: return true;
            case HitOp::Eq:   return n == hitN;
            case HitOp::Ge:   return n >= hitN;
            case HitOp::Gt:   return n > hitN;
            case HitOp::Lt:   return n < hitN;
            case HitOp::Le:   return n <= hitN;
            case HitOp::Mod:  return hitN > 0 && n % hitN == 0;
        }
        return true;
    }
    // Parse VS Code's hitCondition text: `N` or `== N`, `>= N`, `> N`, `< N`,
    // `<= N`, `% N`. False (and `error`) when it is none of these.
    bool parseHitCondition(const std::string& text, std::string& error);
};

// The location breakpoints, shared by every thread of the debuggee: the
// adapter thread rewrites the table while script threads read it before each
// instruction, so every access takes the lock (shared for a lookup). An empty
// table answers without it.
class BreakpointTable {
public:
    bool isSet(const BytecodeModule* m, size_t pc) const { return find(m, pc) != nullptr; }
    // The breakpoint at (m, pc), or null. A breakpoint added without a spec
    // has a plain one.
    std::shared_ptr<BreakpointSpec> find(const BytecodeModule* m, size_t pc) const;
    void add(const BytecodeModule* m, size_t pc, std::shared_ptr<BreakpointSpec> spec = nullptr);
    void remove(const BytecodeModule* m, size_t pc);
    void clear();
    struct Location {
        const BytecodeModule*           module;
        size_t                          pc;
        std::shared_ptr<BreakpointSpec> spec;   // null: plain
    };
    // Replace the whole table in one step, so no thread sees it half built.
    void assign(const std::vector<Location>& entries);
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
    std::unordered_map<Entry, std::shared_ptr<BreakpointSpec>, EntryHash> entries_;
};

} // namespace protoST
