#include "BreakpointTable.h"
namespace protoST {

bool BreakpointTable::isSet(const BytecodeModule* m, size_t pc) const {
    return entries_.find(Entry{m, pc}) != entries_.end();
}
void BreakpointTable::add(const BytecodeModule* m, size_t pc) { entries_.insert(Entry{m, pc}); }
void BreakpointTable::remove(const BytecodeModule* m, size_t pc) { entries_.erase(Entry{m, pc}); }

} // namespace protoST
