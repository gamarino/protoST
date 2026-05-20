#include "HandlerStack.h"
#include "protoCore.h"

#include <atomic>

namespace protoST {

namespace {

// Process-global handler-id counter. Ids are never reused, so an
// UnwindToHandler.handlerId targets exactly one `on:do:` activation for the
// life of the process — even with multiple actor threads pushing handlers.
std::atomic<unsigned long> g_nextHandlerId{1};

// The per-OS-thread handler stack. Innermost (most recently pushed) handler is
// at the back. Workers and the main thread each own an independent vector.
thread_local std::vector<HandlerEntry> g_handlerStack;

} // namespace

unsigned long handlerStackPush(const proto::ProtoObject* guardClass,
                               const proto::ProtoObject* handlerBlock) {
    HandlerEntry e;
    e.guardClass   = guardClass;
    e.handlerBlock = handlerBlock;
    e.handlerId    = g_nextHandlerId.fetch_add(1, std::memory_order_relaxed);
    e.enabled      = true;
    g_handlerStack.push_back(e);
    return e.handlerId;
}

void handlerStackPop(unsigned long handlerId) {
    // Idempotent removal. `on:do:` calls this on every exit path (normal,
    // UnwindToHandler-caught, foreign exception) so the entry may already be
    // gone. Search from the top — the entry being popped is usually the
    // newest, and an exact-id match keeps unrelated nested entries intact.
    for (std::size_t i = g_handlerStack.size(); i-- > 0; ) {
        if (g_handlerStack[i].handlerId == handlerId) {
            g_handlerStack.erase(g_handlerStack.begin() +
                                 static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

// Returns true when `exceptionInstance` is an instance of (identical to, or a
// prototype-chain descendant of) `guardClass`.
static bool matchesGuard(proto::ProtoContext* ctx,
                         const proto::ProtoObject* exceptionInstance,
                         const proto::ProtoObject* guardClass) {
    if (!exceptionInstance || !guardClass) return false;
    if (exceptionInstance == guardClass) return true;
    // hasParent walks the parent chain; non-zero means guardClass is an
    // ancestor of the instance (so the instance is `Error`, a user subclass
    // of `Error`, ... when guardClass is `Error`).
    return exceptionInstance->hasParent(ctx, guardClass) != 0;
}

const HandlerEntry* handlerStackFindMatch(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exceptionInstance) {
    for (std::size_t i = g_handlerStack.size(); i-- > 0; ) {
        const HandlerEntry& e = g_handlerStack[i];
        if (!e.enabled) continue;
        if (matchesGuard(ctx, exceptionInstance, e.guardClass))
            return &g_handlerStack[i];
    }
    return nullptr;
}

std::vector<unsigned long> handlerStackDisableFrom(unsigned long targetHandlerId) {
    std::vector<unsigned long> flipped;
    bool found = false;
    for (std::size_t i = 0; i < g_handlerStack.size(); ++i) {
        HandlerEntry& e = g_handlerStack[i];
        if (e.handlerId == targetHandlerId) found = true;
        if (!found) continue;            // outer than the target — leave enabled
        if (e.enabled) {
            e.enabled = false;
            flipped.push_back(e.handlerId);
        }
    }
    return flipped;
}

void handlerStackRestore(const std::vector<unsigned long>& disabledIds) {
    for (unsigned long id : disabledIds) {
        for (std::size_t i = g_handlerStack.size(); i-- > 0; ) {
            if (g_handlerStack[i].handlerId == id) {
                g_handlerStack[i].enabled = true;
                break;
            }
        }
    }
}

} // namespace protoST
