#include "HandlerStack.h"
#include "protoCore.h"

#include <atomic>

namespace protoST {

namespace {

// Process-global handler-id counter. Ids are never reused, so an
// UnwindToHandler.handlerId targets exactly one `on:do:` activation for the
// life of the process — even with multiple actor threads pushing handlers.
std::atomic<proto::proto_ulong> g_nextHandlerId{1};

// The per-OS-thread handler stack. Innermost (most recently pushed) handler is
// at the back. Workers and the main thread each own an independent vector.
thread_local std::vector<HandlerEntry> g_handlerStack;

} // namespace

proto::proto_ulong handlerStackPush(const proto::ProtoObject* guardClass,
                               const proto::ProtoObject* handlerBlock) {
    HandlerEntry e;
    e.guardClass   = guardClass;
    e.handlerBlock = handlerBlock;
    e.handlerId    = g_nextHandlerId.fetch_add(1, std::memory_order_relaxed);
    e.enabled      = true;
    g_handlerStack.push_back(e);
    return e.handlerId;
}

void handlerStackPop(proto::proto_ulong handlerId) {
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
    // An ExceptionSet (`ZeroDivide, MessageNotUnderstood`, built in the kernel)
    // matches when any of its classes does. Its classes are held in the
    // instance variable exceptionClasses, stored as `_iv_exceptionClasses`
    // holding an Array.
    const proto::ProtoObject* members = guardClass->getAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "_iv_exceptionClasses"));
    if (members && members != PROTO_NONE) {
        const proto::ProtoObject* data = members->getAttribute(
            ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
        const proto::ProtoList* list = (data && data != PROTO_NONE) ? data->asList(ctx) : nullptr;
        if (list) {
            for (proto::proto_ulong i = 0; i < list->getSize(ctx); ++i)
                if (matchesGuard(ctx, exceptionInstance, list->getAt(ctx, static_cast<int>(i))))
                    return true;
            return false;
        }
    }
    // hasParent walks the parent chain; non-zero means guardClass is an
    // ancestor of the instance (so the instance is `Error`, a user subclass
    // of `Error`, ... when guardClass is `Error`).
    return exceptionInstance->hasParent(ctx, guardClass) != 0;
}

const HandlerEntry* handlerStackFindMatch(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* exceptionInstance,
                                          proto::proto_ulong searchBelowId) {
    // When `searchBelowId` is set (EXC-b `pass`), locate that entry's stack
    // index and start the search strictly OUTER to it — skipping the entry
    // itself and everything inner. If the id is no longer present (already
    // popped) the search falls back to the whole stack.
    std::size_t startIdx = g_handlerStack.size();
    if (searchBelowId != 0) {
        for (std::size_t i = g_handlerStack.size(); i-- > 0; ) {
            if (g_handlerStack[i].handlerId == searchBelowId) {
                startIdx = i;   // search begins below this index
                break;
            }
        }
    }
    for (std::size_t i = startIdx; i-- > 0; ) {
        const HandlerEntry& e = g_handlerStack[i];
        if (!e.enabled) continue;
        if (matchesGuard(ctx, exceptionInstance, e.guardClass))
            return &g_handlerStack[i];
    }
    return nullptr;
}

std::vector<proto::proto_ulong> handlerStackDisableFrom(proto::proto_ulong targetHandlerId) {
    std::vector<proto::proto_ulong> flipped;
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

std::vector<proto::proto_ulong> handlerStackDisableAll() {
    std::vector<proto::proto_ulong> flipped;
    for (HandlerEntry& e : g_handlerStack) {
        if (e.enabled) {
            e.enabled = false;
            flipped.push_back(e.handlerId);
        }
    }
    return flipped;
}

void handlerStackRestore(const std::vector<proto::proto_ulong>& disabledIds) {
    for (proto::proto_ulong id : disabledIds) {
        for (std::size_t i = g_handlerStack.size(); i-- > 0; ) {
            if (g_handlerStack[i].handlerId == id) {
                g_handlerStack[i].enabled = true;
                break;
            }
        }
    }
}

} // namespace protoST
