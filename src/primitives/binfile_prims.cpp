#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "primitives/BytesSupport.h"
#include "primitives/IoBrackets.h"
#include "runtime/Bootstrap.h"
#include "protoCore.h"

#include <protoio/file.h>
#include <protoio/stream.h>

#include <cstdint>
#include <memory>
#include <exception>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace protoST {

// BinaryFile (docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md,
// section 4.4): a positionless binary file, the storage ground of protoBaseST.
// The class itself is protoST code (lib/kernel/binfile.st); these primitives
// bind protoio::file's positioned and durable operations and never
// re-implement them.
//
// A BinaryFile is a handle to an OS resource, not a value. Smalltalk sees an
// Integer handle that is never reused; the descriptor lives in an OpenFile
// held by a registry here, through a shared_ptr:
//   * every operation takes its own reference to the OpenFile before it
//     leaves the collector's quorum, and drops it inside the bracket;
//   * close removes the entry from the registry, so any later use of the
//     handle signals Error "BinaryFile is closed", and a second close is a
//     no-op;
//   * the descriptor itself is closed when the last reference goes. A close
//     while another thread is still inside readAt, append, sync or lock on
//     the same file therefore never lets the number be reused under that
//     call: it completes on the old file and only then is the descriptor
//     closed. lock and tryLock re-check the handle before answering success
//     (see prim_BinFileTryLock).
//   * the registry's mutex guards only the map: it is never held across a
//     protoCore allocation, nor across a protoIO call.
//
// An unclosed BinaryFile keeps its descriptor (and its lock) until the
// process exits: nothing is closed when the collector frees the Smalltalk
// object, so a file is never closed silently. The registry is never
// destroyed, so exit does not run protoIO from a static destructor; the OS
// closes the descriptors.
//
// Which bracket each call uses (spec section 3): readAt, writeAt, append,
// size, truncate, tryLock, unlock and close are local and bounded, so they
// run unmanaged(); lock (another holder may keep the lock without bound),
// sync and dataSync (they wait for the storage device) run blocking(), which
// also lets the worker pool add a worker while this one waits. Opening runs
// blocking() as __fileOpen:mode: does: opening a FIFO waits for its other end.

namespace {

using PO = proto::ProtoObject;

// One open descriptor. It is closed when the last user lets it go.
struct OpenFile {
    const int fd;
    // Set while an append: runs on this file. The one-appender contract is
    // enforced in process: a second concurrent append: fails loudly instead
    // of corrupting the offsets. Another process cannot be detected.
    std::atomic<bool> appending{false};
    explicit OpenFile(int d) : fd(d) {}
    ~OpenFile() {
        try { protoio::close(fd); } catch (...) {}
    }
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;
};

// Claims OpenFile::appending by CAS and releases it on every exit path.
class AppendClaim {
public:
    // `refusal` is the Error text when the claim is already taken.
    AppendClaim(OpenFile& f, const char* refusal) : file_(f) {
        bool expected = false;
        if (!file_.appending.compare_exchange_strong(expected, true, std::memory_order_acquire))
            throw std::runtime_error(refusal);
    }
    ~AppendClaim() { file_.appending.store(false, std::memory_order_release); }
    AppendClaim(const AppendClaim&) = delete;
    AppendClaim& operator=(const AppendClaim&) = delete;
private:
    OpenFile& file_;
};

struct Registry {
    std::mutex mutex;
    long long next = 1;                                              // guarded by mutex
    std::unordered_map<long long, std::shared_ptr<OpenFile>> files;  // guarded by mutex
};

// Never destroyed (see above).
Registry& registry() {
    static Registry* r = new Registry;
    return *r;
}

long long smallIntegerArg(proto::ProtoContext* ctx, const PO* v, const char* who, const char* what) {
    if (!v || v == PROTO_NONE || !v->isInteger(ctx))
        throw std::runtime_error(std::string(who) + ": " + what + ": an Integer was expected");
    if (!proto::isSmallInt(v))
        throw std::runtime_error(std::string(who) + ": " + what + " is out of range");
    return v->asLong(ctx);
}

long long handleArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    return smallIntegerArg(ctx, v, who, "the handle");
}

// The OpenFile of a handle, or Error "BinaryFile is closed".
std::shared_ptr<OpenFile> openFile(proto::ProtoContext* ctx, const PO* h, const char* who) {
    const long long handle = handleArg(ctx, h, who);
    Registry& reg = registry();
    {
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto it = reg.files.find(handle);
        if (it != reg.files.end()) return it->second;
    }
    throw std::runtime_error(std::string(who) + ": BinaryFile is closed");
}

// Whether the handle is still open. Touches no protoCore object, so it may
// run inside a bracket.
bool isRegistered(long long handle) {
    Registry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.files.count(handle) != 0;
}

// The bytes a write takes, copied before the call leaves the collector's
// quorum (an unmanaged thread touches no protoCore object).
std::string bytesArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    if (!v || v == PROTO_NONE || !v->isByteBuffer(ctx))
        throw std::runtime_error(std::string(who) + ": a ByteArray was expected");
    return std::string(bytesView(ctx, v));
}

// Run op(fd) on the file in the named bracket. The file's reference is
// moved into the bracket, so that, if a close came meanwhile, the descriptor
// is closed there too, outside the collector's quorum. Each primitive checks
// its other arguments before it takes the reference, so nothing between
// openFile and the bracket can throw.
template <typename Op>
auto onFileUnmanaged(proto::ProtoContext* ctx, std::shared_ptr<OpenFile> file, Op&& op) {
    return unmanaged(ctx, [&] {
        const std::shared_ptr<OpenFile> held = std::move(file);
        return op(held->fd);
    });
}

template <typename Op>
auto onFileBlocking(STRuntime& rt, proto::ProtoContext* ctx, std::shared_ptr<OpenFile> file, Op&& op) {
    return blocking(rt, ctx, [&] {
        const std::shared_ptr<OpenFile> held = std::move(file);
        return op(held->fd);
    });
}

#define PRIM(NAME) const PO* NAME(STRuntime& rt, proto::ProtoContext* ctx, const PO* r, \
                                   const PO* const* a, int argc)
#define ARGS(N, SEL) if (argc != (N)) throw std::runtime_error(SEL " expects " #N " argument(s)"); \
                     (void) rt; (void) r; (void) a

// The bits of __binFileOpen:flags:, in the order of the option symbols in
// lib/kernel/binfile.st: #read #write #create #exclusive #truncate #append.
enum : long long { kRead = 1, kWrite = 2, kCreate = 4, kExclusive = 8, kTruncate = 16, kAppend = 32 };

// __binFileOpen: path flags: anInteger — answers a new handle.
PRIM(prim_BinFileOpen) {
    ARGS(2, "__binFileOpen:flags:");
    const proto::ProtoString* s = a[0] ? a[0]->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error("BinaryFile class>>open:options: a path String was expected");
    const std::string path = s->toStdString(ctx);
    const long long flags = smallIntegerArg(ctx, a[1], "BinaryFile class>>open:options:", "the flags");
    protoio::file::OpenOptions o;
    o.read = flags & kRead;
    o.write = flags & kWrite;
    o.create = flags & kCreate;
    o.exclusive = flags & kExclusive;
    o.truncate = flags & kTruncate;
    o.append = flags & kAppend;
    const int fd = blocking(rt, ctx, [&] { return protoio::file::open(path, o); });
    // From here the descriptor is owned: by the catch below until the
    // OpenFile exists, then by the OpenFile. The map receives a copy of the
    // reference, so if emplace throws, the last reference is the local one.
    // On either failure the descriptor is closed after the registry mutex is
    // released and inside unmanaged(), like every other close.
    std::shared_ptr<OpenFile> file;
    try {
        file = std::make_shared<OpenFile>(fd);
    } catch (...) {
        unmanaged(ctx, [&] { try { protoio::close(fd); } catch (...) {} });
        throw;
    }
    Registry& reg = registry();
    long long handle = 0;
    std::exception_ptr failed;
    try {
        std::lock_guard<std::mutex> lock(reg.mutex);
        handle = reg.next++;
        reg.files.emplace(handle, file);
    } catch (...) {
        failed = std::current_exception();  // the guard has already released the mutex
    }
    if (failed) {
        unmanaged(ctx, [&] { file.reset(); });
        std::rethrow_exception(failed);
    }
    return ctx->fromLong(handle);
}

// __binFileAppend: h data: aByteArray — answers the offset where it starts.
PRIM(prim_BinFileAppend) {
    ARGS(2, "__binFileAppend:data:");
    const char* who = "BinaryFile>>append:";
    const std::string data = bytesArg(ctx, a[1], who);
    auto file = openFile(ctx, a[0], who);
    // The claim lives inside the bracket, before the held reference: it is
    // released first, then the reference (and a pending close) is dropped.
    const std::int64_t at = unmanaged(ctx, [&] {
        const std::shared_ptr<OpenFile> held = std::move(file);
        AppendClaim claim(*held, "BinaryFile>>append: another append is in progress on this BinaryFile "
                                 "(one appender at a time)");
        return protoio::file::appendData(held->fd, data);
    });
    return ctx->fromLong(static_cast<long long>(at));
}

// __binFileReadAt: h offset: o size: n — a ByteArray, shorter only at the
// end of the file (empty from the end on).
PRIM(prim_BinFileReadAt) {
    ARGS(3, "__binFileReadAt:offset:size:");
    const char* who = "BinaryFile>>readAt:size:";
    const long long offset = smallIntegerArg(ctx, a[1], who, "the offset");
    const long long count = smallIntegerArg(ctx, a[2], who, "the size");
    if (count < 0) throw std::runtime_error(std::string(who) + ": the size must not be negative");
    auto file = openFile(ctx, a[0], who);
    const std::string got = onFileUnmanaged(ctx, std::move(file),
        [&](int fd) { return protoio::file::readAt(fd, offset, static_cast<std::size_t>(count)); });
    return makeBytes(ctx, got);
}

// __binFileWriteAt: h offset: o data: aByteArray
PRIM(prim_BinFileWriteAt) {
    ARGS(3, "__binFileWriteAt:offset:data:");
    const char* who = "BinaryFile>>writeAt:bytes:";
    const long long offset = smallIntegerArg(ctx, a[1], who, "the offset");
    const std::string data = bytesArg(ctx, a[2], who);
    auto file = openFile(ctx, a[0], who);
    // A write that extends the file moves its end like an append does, so it
    // takes the same claim. A write inside the file cannot move the end (the
    // size only grows meanwhile), so overwrites stay concurrent.
    unmanaged(ctx, [&] {
        const std::shared_ptr<OpenFile> held = std::move(file);
        const bool extends = offset + static_cast<long long>(data.size()) > protoio::file::size(held->fd);
        if (extends) {
            AppendClaim claim(*held, "BinaryFile>>writeAt:bytes: a write that extends the file cannot run while "
                                     "another append is in progress on this BinaryFile (one appender at a time)");
            protoio::file::writeAt(held->fd, offset, data);
        } else {
            protoio::file::writeAt(held->fd, offset, data);
        }
    });
    return r;
}

PRIM(prim_BinFileSize) {
    ARGS(1, "__binFileSize:");
    auto file = openFile(ctx, a[0], "BinaryFile>>size");
    const std::int64_t n = onFileUnmanaged(ctx, std::move(file),
        [](int fd) { return protoio::file::size(fd); });
    return ctx->fromLong(static_cast<long long>(n));
}

// __binFileTruncate: h to: n — cuts the file, or extends it with zeros.
PRIM(prim_BinFileTruncate) {
    ARGS(2, "__binFileTruncate:to:");
    const char* who = "BinaryFile>>truncateTo:";
    const long long n = smallIntegerArg(ctx, a[1], who, "the size");
    auto file = openFile(ctx, a[0], who);
    onFileUnmanaged(ctx, std::move(file),
        [&](int fd) { protoio::file::truncate(fd, n); });
    return r;
}

PRIM(prim_BinFileSync) {
    ARGS(1, "__binFileSync:");
    auto file = openFile(ctx, a[0], "BinaryFile>>sync");
    onFileBlocking(rt, ctx, std::move(file),
        [](int fd) { protoio::file::sync(fd); });
    return r;
}

PRIM(prim_BinFileDataSync) {
    ARGS(1, "__binFileDataSync:");
    auto file = openFile(ctx, a[0], "BinaryFile>>dataSync");
    onFileBlocking(rt, ctx, std::move(file),
        [](int fd) { protoio::file::dataSync(fd); });
    return r;
}

// tryLock and lock answer success only while the handle is still open. A
// close that came while the call was inside flock (lock waits without bound
// for the other holder) leaves this call holding the last reference: the
// descriptor would close, and the lock with it, right after a success was
// answered, and the caller would believe it owned a lock nobody holds (for
// protoBaseST, ProtoBase's OwnerLock). So, still holding the reference, the
// call re-checks the registry once flock returns; if the handle was closed
// meanwhile, it releases the lock itself and signals Error. A close after
// that check is an ordinary close of a locked file, ordered after the lock.
// The wait itself is not cut short: flock cannot be portably interrupted.
PRIM(prim_BinFileTryLock) {
    ARGS(1, "__binFileTryLock:");
    const long long handle = handleArg(ctx, a[0], "BinaryFile>>tryLock");
    auto file = openFile(ctx, a[0], "BinaryFile>>tryLock");
    bool closedMeanwhile = false;
    const bool got = onFileUnmanaged(ctx, std::move(file), [&](int fd) {
        if (!protoio::file::tryLock(fd)) return false;
        if (isRegistered(handle)) return true;
        closedMeanwhile = true;
        protoio::file::unlock(fd);
        return false;
    });
    if (closedMeanwhile)
        throw std::runtime_error("BinaryFile>>tryLock: BinaryFile is closed (the lock was released)");
    return got ? PROTO_TRUE : PROTO_FALSE;
}

PRIM(prim_BinFileLock) {
    ARGS(1, "__binFileLock:");
    const long long handle = handleArg(ctx, a[0], "BinaryFile>>lock");
    auto file = openFile(ctx, a[0], "BinaryFile>>lock");
    const bool held = onFileBlocking(rt, ctx, std::move(file), [&](int fd) {
        protoio::file::lock(fd);
        if (isRegistered(handle)) return true;
        protoio::file::unlock(fd);
        return false;
    });
    if (!held) throw std::runtime_error("BinaryFile>>lock: BinaryFile is closed (the lock was released)");
    return r;
}

PRIM(prim_BinFileUnlock) {
    ARGS(1, "__binFileUnlock:");
    auto file = openFile(ctx, a[0], "BinaryFile>>unlock");
    onFileUnmanaged(ctx, std::move(file),
        [](int fd) { protoio::file::unlock(fd); });
    return r;
}

// __binFileClose: h — idempotent. The descriptor is closed here, or by the
// last operation still running on it.
PRIM(prim_BinFileClose) {
    ARGS(1, "__binFileClose:");
    const long long handle = handleArg(ctx, a[0], "BinaryFile>>close");
    std::shared_ptr<OpenFile> file;
    Registry& reg = registry();
    {
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto it = reg.files.find(handle);
        if (it == reg.files.end()) return r;
        file = std::move(it->second);
        reg.files.erase(it);
    }
    unmanaged(ctx, [&] { file.reset(); });
    return r;
}

PRIM(prim_BinFileIsOpen) {
    ARGS(1, "__binFileIsOpen:");
    const long long handle = handleArg(ctx, a[0], "BinaryFile>>isClosed");
    return isRegistered(handle) ? PROTO_TRUE : PROTO_FALSE;
}

#undef ARGS
#undef PRIM

} // anon

void installBinaryFilePrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    const proto::ProtoObject* o = rt.bootstrap().objectProto;
    struct Entry { const char* sel; PrimFn fn; };
    const Entry entries[] = {
        {"__binFileOpen:flags:", prim_BinFileOpen},
        {"__binFileAppend:data:", prim_BinFileAppend},
        {"__binFileReadAt:offset:size:", prim_BinFileReadAt},
        {"__binFileWriteAt:offset:data:", prim_BinFileWriteAt},
        {"__binFileSize:", prim_BinFileSize},
        {"__binFileTruncate:to:", prim_BinFileTruncate},
        {"__binFileSync:", prim_BinFileSync},
        {"__binFileDataSync:", prim_BinFileDataSync},
        {"__binFileTryLock:", prim_BinFileTryLock},
        {"__binFileLock:", prim_BinFileLock},
        {"__binFileUnlock:", prim_BinFileUnlock},
        {"__binFileClose:", prim_BinFileClose},
        {"__binFileIsOpen:", prim_BinFileIsOpen},
    };
    for (const Entry& e : entries) bindPrimitive(rt, o, e.sel, reg.registerPrim(e.fn));
}

} // namespace protoST
