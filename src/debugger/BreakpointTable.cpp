#include "BreakpointTable.h"
#include <cctype>
#include <cstdlib>
#include <mutex>
namespace protoST {

bool BreakpointSpec::parseHitCondition(const std::string& text, std::string& error) {
    size_t i = 0;
    auto skip = [&] { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; };
    skip();
    if (i == text.size()) { hitOp = HitOp::None; return true; }
    HitOp op = HitOp::Eq;
    if (text.compare(i, 2, "==") == 0)      { op = HitOp::Eq;  i += 2; }
    else if (text.compare(i, 2, ">=") == 0) { op = HitOp::Ge;  i += 2; }
    else if (text.compare(i, 2, "<=") == 0) { op = HitOp::Le;  i += 2; }
    else if (text[i] == '>')                { op = HitOp::Gt;  i += 1; }
    else if (text[i] == '<')                { op = HitOp::Lt;  i += 1; }
    else if (text[i] == '%')                { op = HitOp::Mod; i += 1; }
    skip();
    size_t start = i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    const bool haveDigits = i > start;
    long n = haveDigits ? std::strtol(text.c_str() + start, nullptr, 10) : 0;
    skip();
    if (!haveDigits || i != text.size() || (op == HitOp::Mod && n == 0)) {
        error = "invalid hit condition '" + text +
                "': use N, == N, >= N, > N, < N, <= N or % N (N a positive integer)";
        return false;
    }
    hitOp = op;
    hitN  = n;
    return true;
}

std::shared_ptr<BreakpointSpec> BreakpointTable::find(const BytecodeModule* m, size_t pc) const {
    if (count_.load(std::memory_order_relaxed) == 0) return nullptr;
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = entries_.find(Entry{m, pc});
    return it == entries_.end() ? nullptr : it->second;
}
void BreakpointTable::add(const BytecodeModule* m, size_t pc, std::shared_ptr<BreakpointSpec> spec) {
    if (!spec) spec = std::make_shared<BreakpointSpec>();
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_[Entry{m, pc}] = std::move(spec);
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
void BreakpointTable::assign(const std::vector<Location>& entries) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    entries_.clear();
    for (const auto& e : entries)
        entries_[Entry{e.module, e.pc}] = e.spec ? e.spec : std::make_shared<BreakpointSpec>();
    count_.store(entries_.size(), std::memory_order_relaxed);
}
size_t BreakpointTable::size() const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return entries_.size();
}

} // namespace protoST
