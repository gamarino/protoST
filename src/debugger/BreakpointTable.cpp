#include "BreakpointTable.h"
#include <mutex>
namespace protoST {

bool BreakpointTable::isSet(const BytecodeModule* m, size_t pc) const {
    if (count_.load(std::memory_order_relaxed) == 0) return false;
    std::shared_lock<std::shared_mutex> lk(mu_);
    return entries_.find(Entry{m, pc}) != entries_.end();
}
void BreakpointTable::add(const BytecodeModule* m, size_t pc) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_.insert(Entry{m, pc});
    count_.store(entries_.size(), std::memory_order_relaxed);
}
void BreakpointTable::remove(const BytecodeModule* m, size_t pc) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_.erase(Entry{m, pc});
    count_.store(entries_.size(), std::memory_order_relaxed);
}
void BreakpointTable::clear() {
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_.clear();
    count_.store(0, std::memory_order_relaxed);
}
void BreakpointTable::assign(const std::vector<std::pair<const BytecodeModule*, size_t>>& entries) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_.clear();
    for (const auto& e : entries) entries_.insert(Entry{e.first, e.second});
    count_.store(entries_.size(), std::memory_order_relaxed);
}
size_t BreakpointTable::size() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return entries_.size();
}

} // namespace protoST
