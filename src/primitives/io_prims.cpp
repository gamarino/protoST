#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "runtime/ZeroDivideSignal.h"
#include "protoCore.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

extern char** environ;

namespace protoST {

// Input and output (docs/superpowers/specs/2026-09-29-io-design.md).
//
// Low-level primitives bound on `objectProto`; the classes a program uses
// (Stdio, FileReference, OSProcess in lib/kernel/io.st; Socket, ServerSocket,
// UDPSocket in lib/net.st; HTTPClient, HTTPServer in lib/http.st) are protoST
// code on top of them. Streams are integers: POSIX file descriptors.
//
// Rules every primitive here follows:
//   * arguments are copied into C++ values BEFORE anything blocks;
//   * every call that can block (read, write, accept, connect, name lookup,
//     waiting for a child process) runs inside a ProtoContext::UnmanagedScope
//     and touches no protoCore object there, so a thread blocked on I/O never
//     holds up a collection (the ProtoThread convention `sleep:` follows);
//   * protoCore values are built only after the blocking part returns;
//   * descriptors are opened close-on-exec, so child processes inherit only
//     their standard streams;
//   * failures raise classed, catchable errors (FileDoesNotExist,
//     ConnectionRefused, ...), declared in lib/kernel/io.st.

namespace {

namespace fs = std::filesystem;
using PO = proto::ProtoObject;

// ---------------------------------------------------------------- arguments

std::string g_programPath;
std::vector<std::string> g_programArgs;

// ------------------------------------------------------------------ helpers

std::string str(proto::ProtoContext* ctx, const PO* v, const char* who) {
    const proto::ProtoString* s = v ? v->asString(ctx) : nullptr;
    if (!s) throw std::runtime_error(std::string(who) + ": a String was expected");
    return s->toStdString(ctx);
}

long long num(proto::ProtoContext* ctx, const PO* v, const char* who) {
    if (!v || !v->isInteger(ctx)) throw std::runtime_error(std::string(who) + ": an Integer was expected");
    return v->asLong(ctx);
}

bool truthy(const PO* v) { return v == PROTO_TRUE; }

const PO* boolean(bool b) { return b ? PROTO_TRUE : PROTO_FALSE; }

const PO* string(proto::ProtoContext* ctx, const std::string& s) {
    return ctx->fromUTF8String(s.c_str());
}

// The elements of an Array (its `__data__` list), or of a bare list.
std::vector<const PO*> elements(proto::ProtoContext* ctx, const PO* arr, const char* who) {
    std::vector<const PO*> out;
    if (!arr || arr == PROTO_NONE) return out;
    const proto::ProtoList* l = nullptr;
    const PO* d = arr->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
    if (d && d != PROTO_NONE) l = d->asList(ctx);
    if (!l && !arr->asString(ctx)) l = arr->asList(ctx);
    if (!l) throw std::runtime_error(std::string(who) + ": an Array was expected");
    const unsigned long n = l->getSize(ctx);
    out.reserve(n);
    for (unsigned long i = 0; i < n; ++i) out.push_back(l->getAt(ctx, static_cast<int>(i)));
    return out;
}

std::vector<std::string> stringArray(proto::ProtoContext* ctx, const PO* arr, const char* who) {
    std::vector<std::string> out;
    for (const PO* e : elements(ctx, arr, who)) out.push_back(str(ctx, e, who));
    return out;
}

// An Array built from `items`, made inside a critical section so the
// young, unrooted elements cannot be handed to the collector mid-build.
const PO* makeArray(STRuntime& rt, proto::ProtoContext* ctx, const std::vector<const PO*>& items) {
    proto::ProtoContext::CriticalSection cs(ctx);
    const proto::ProtoList* l = ctx->newList();
    for (const PO* e : items) l = l->appendLast(ctx, e);
    const PO* arr = rt.bootstrap().arrayProto->newChild(ctx, /*isMutable=*/true);
    arr->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"), l->asObject(ctx));
    return arr;
}

const PO* stringsArray(STRuntime& rt, proto::ProtoContext* ctx, const std::vector<std::string>& v) {
    proto::ProtoContext::CriticalSection cs(ctx);
    std::vector<const PO*> items;
    items.reserve(v.size());
    for (const std::string& s : v) items.push_back(string(ctx, s));
    return makeArray(rt, ctx, items);
}

const PO* bytesArray(STRuntime& rt, proto::ProtoContext* ctx, const std::string& bytes) {
    proto::ProtoContext::CriticalSection cs(ctx);
    std::vector<const PO*> items;
    items.reserve(bytes.size());
    for (unsigned char c : bytes) items.push_back(ctx->fromLong(c));
    return makeArray(rt, ctx, items);
}

std::string bytesOf(proto::ProtoContext* ctx, const PO* arr, const char* who) {
    std::string out;
    for (const PO* e : elements(ctx, arr, who)) {
        const long long b = num(ctx, e, who);
        if (b < 0 || b > 255) throw std::runtime_error(std::string(who) + ": bytes are integers from 0 to 255");
        out.push_back(static_cast<char>(b));
    }
    return out;
}

[[noreturn]] void fileError(const std::string& path, int err, const char* action) {
    const std::string msg = std::string(action) + " " + path + ": " + std::strerror(err);
    if (err == ENOENT) throw ClassedErrorSignal("FileDoesNotExist", msg);
    if (err == EEXIST) throw ClassedErrorSignal("FileAlreadyExists", msg);
    throw ClassedErrorSignal("FileSystemError", msg);
}

[[noreturn]] void fsError(const fs::filesystem_error& e, const char* action) {
    fileError(e.path1().string(), e.code().value(), action);
}

[[noreturn]] void netError(int err, const std::string& what) {
    const std::string msg = what + ": " + std::strerror(err);
    if (err == ECONNREFUSED) throw ClassedErrorSignal("ConnectionRefused", msg);
    if (err == ETIMEDOUT || err == EAGAIN || err == EWOULDBLOCK)
        throw ClassedErrorSignal("ConnectionTimedOut", msg);
    throw ClassedErrorSignal("NetworkError", msg);
}

// ------------------------------------------------------------ descriptors
//
// One buffered reader per descriptor (files, sockets, stdin), so `nextLine`
// can read ahead; a socket upgraded to TLS keeps its SSL object here.

struct FdState {
    std::string buf;
    bool eof = false;
    SSL* ssl = nullptr;
    int timeoutMs = -1;   // -1: block without limit
    bool isSocket = false;
};

std::mutex g_fdMutex;
std::unordered_map<int, std::shared_ptr<FdState>> g_fds;

std::shared_ptr<FdState> fdState(int fd) {
    std::lock_guard<std::mutex> lock(g_fdMutex);
    auto& p = g_fds[fd];
    if (!p) {
        p = std::make_shared<FdState>();
        struct stat st{};
        if (::fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode)) p->isSocket = true;
    }
    return p;
}

void forgetFd(int fd) {
    std::lock_guard<std::mutex> lock(g_fdMutex);
    g_fds.erase(fd);
}

// Waits until `fd` is readable (or writable) within the state's timeout.
// Called unmanaged.
bool waitReady(int fd, short events, int timeoutMs) {
    if (timeoutMs < 0) return true;
    pollfd p{fd, events, 0};
    for (;;) {
        const int r = ::poll(&p, 1, timeoutMs);
        if (r < 0 && errno == EINTR) continue;
        return r > 0;
    }
}

// Reads up to `n` bytes into `out`; answers the count, 0 at end of stream.
// Called unmanaged.
ssize_t rawRead(int fd, FdState& st, char* out, size_t n) {
    if (st.ssl) {
        if (SSL_pending(st.ssl) == 0 && !waitReady(fd, POLLIN, st.timeoutMs)) { errno = ETIMEDOUT; return -1; }
        for (;;) {
            const int r = SSL_read(st.ssl, out, static_cast<int>(n));
            if (r > 0) return r;
            const int e = SSL_get_error(st.ssl, r);
            if (e == SSL_ERROR_ZERO_RETURN) return 0;
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            if (e == SSL_ERROR_SYSCALL && r == 0) return 0;
            errno = EIO;
            return -1;
        }
    }
    if (!waitReady(fd, POLLIN, st.timeoutMs)) { errno = ETIMEDOUT; return -1; }
    for (;;) {
        const ssize_t r = ::read(fd, out, n);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

// Writes all of `data`. Called unmanaged.
void rawWrite(int fd, FdState& st, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        ssize_t w;
        if (st.ssl) {
            const int r = SSL_write(st.ssl, data.data() + done, static_cast<int>(data.size() - done));
            if (r <= 0) {
                const int e = SSL_get_error(st.ssl, r);
                if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
                netError(EIO, "TLS write");
            }
            w = r;
        } else {
            if (!waitReady(fd, POLLOUT, st.timeoutMs)) netError(ETIMEDOUT, "write");
            w = st.isSocket ? ::send(fd, data.data() + done, data.size() - done, MSG_NOSIGNAL)
                            : ::write(fd, data.data() + done, data.size() - done);
            if (w < 0) {
                if (errno == EINTR) continue;
                if (st.isSocket) netError(errno, "write");
                fileError("descriptor " + std::to_string(fd), errno, "cannot write to");
            }
        }
        done += static_cast<size_t>(w);
    }
}

// Reads more input into the buffer; false at end of stream. Unmanaged.
bool fill(int fd, FdState& st) {
    if (st.eof) return false;
    char chunk[65536];
    const ssize_t r = rawRead(fd, st, chunk, sizeof chunk);
    if (r < 0) {
        if (st.isSocket || st.ssl) netError(errno, "read");
        fileError("descriptor " + std::to_string(fd), errno, "cannot read from");
    }
    if (r == 0) { st.eof = true; return false; }
    st.buf.append(chunk, static_cast<size_t>(r));
    return true;
}

// ------------------------------------------------------------ OpenSSL

SSL_CTX* tlsContext(bool verify) {
    static std::once_flag once;
    static SSL_CTX* verifying = nullptr;
    static SSL_CTX* trusting = nullptr;
    std::call_once(once, [] {
        OPENSSL_init_ssl(0, nullptr);
        verifying = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_default_verify_paths(verifying);
        SSL_CTX_set_verify(verifying, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_min_proto_version(verifying, TLS1_2_VERSION);
        trusting = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(trusting, SSL_VERIFY_NONE, nullptr);
        SSL_CTX_set_min_proto_version(trusting, TLS1_2_VERSION);
    });
    return verify ? verifying : trusting;
}

std::string tlsErrorText() {
    unsigned long e = ERR_get_error();
    if (!e) return "TLS handshake failed";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof buf);
    return buf;
}

// --------------------------------------------------------------- address

// Resolves host:port. Unmanaged. Throws NameLookupFailure.
struct AddrList {
    addrinfo* head = nullptr;
    ~AddrList() { if (head) ::freeaddrinfo(head); }
};

void resolve(const std::string& host, int port, int socktype, bool passive, AddrList& out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    if (passive) hints.ai_flags = AI_PASSIVE;
    const std::string service = std::to_string(port);
    const int r = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &out.head);
    if (r != 0) throw ClassedErrorSignal("NameLookupFailure", "cannot resolve " + host + ": " + ::gai_strerror(r));
}

std::string addrText(const sockaddr* sa, int* portOut) {
    char host[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    if (sa->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(sa);
        ::inet_ntop(AF_INET, &in->sin_addr, host, sizeof host);
        port = ntohs(in->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        auto* in = reinterpret_cast<const sockaddr_in6*>(sa);
        ::inet_ntop(AF_INET6, &in->sin6_addr, host, sizeof host);
        port = ntohs(in->sin6_port);
    }
    if (portOut) *portOut = port;
    return host;
}

// Accounts a blocking call on a pool worker, so the scheduler can add a
// worker while this one waits (STRuntime::enterBlockingIO). Constructed
// before the UnmanagedScope: adding a worker allocates.
struct BlockingIO {
    STRuntime& rt;
    bool active;
    BlockingIO(STRuntime& r, proto::ProtoContext* ctx) : rt(r), active(r.enterBlockingIO(ctx)) {}
    ~BlockingIO() { if (active) rt.leaveBlockingIO(); }
};

// ============================================================ primitives

#define PRIM(NAME) const PO* NAME(STRuntime& rt, proto::ProtoContext* ctx, const PO* r, \
                                   const PO* const* a, int argc)
#define ARGS(N, SEL) if (argc != (N)) throw std::runtime_error(SEL " expects " #N " argument(s)"); \
                     (void) rt; (void) r; (void) a

// ---------------------------------------------------------------- system

PRIM(prim_Arguments) { ARGS(0, "__osArguments"); return stringsArray(rt, ctx, g_programArgs); }
PRIM(prim_ProgramPath) { ARGS(0, "__osProgramPath"); return string(ctx, g_programPath); }

PRIM(prim_Getenv) {
    ARGS(1, "__osGetenv:");
    const char* v = std::getenv(str(ctx, a[0], "getenv:").c_str());
    return v ? string(ctx, v) : PROTO_NONE;
}

PRIM(prim_Setenv) {
    ARGS(2, "__osSetenv:to:");
    const std::string k = str(ctx, a[0], "setenv:to:");
    if (a[1] == PROTO_NONE) ::unsetenv(k.c_str());
    else ::setenv(k.c_str(), str(ctx, a[1], "setenv:to:").c_str(), 1);
    return r;
}

PRIM(prim_Environment) {
    ARGS(0, "__osEnvironment");
    std::vector<std::string> v;
    for (char** e = environ; e && *e; ++e) v.emplace_back(*e);
    return stringsArray(rt, ctx, v);
}

PRIM(prim_Exit) {
    ARGS(1, "__osExit:");
    const long long code = num(ctx, a[0], "exit:");
    std::fflush(nullptr);
    ::_exit(static_cast<int>(code & 0xff));
}

PRIM(prim_Pid) { ARGS(0, "__osPid"); return ctx->fromLong(::getpid()); }

PRIM(prim_HostName) {
    ARGS(0, "__osHostName");
    char buf[256] = {0};
    ::gethostname(buf, sizeof buf - 1);
    return string(ctx, buf);
}

PRIM(prim_Platform) {
    ARGS(0, "__osPlatform");
#if defined(__linux__)
    return string(ctx, "linux");
#elif defined(__APPLE__)
    return string(ctx, "macos");
#else
    return string(ctx, "unix");
#endif
}

PRIM(prim_Cwd) {
    ARGS(0, "__osCwd");
    std::error_code ec;
    const fs::path p = fs::current_path(ec);
    if (ec) fileError(".", ec.value(), "cannot read the working directory");
    return string(ctx, p.string());
}

PRIM(prim_Chdir) {
    ARGS(1, "__osChdir:");
    const std::string p = str(ctx, a[0], "changeDirectory:");
    if (::chdir(p.c_str()) != 0) fileError(p, errno, "cannot change directory to");
    return r;
}

// ------------------------------------------------------------ descriptors

PRIM(prim_FdReadLine) {
    ARGS(1, "__fdReadLine:");
    const int fd = static_cast<int>(num(ctx, a[0], "nextLine"));
    auto st = fdState(fd);
    std::string line;
    bool got = false;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        size_t pos;
        while ((pos = st->buf.find('\n')) == std::string::npos) {
            if (!fill(fd, *st)) break;
        }
        pos = st->buf.find('\n');
        if (pos != std::string::npos) {
            line = st->buf.substr(0, pos);
            st->buf.erase(0, pos + 1);
            got = true;
        } else if (!st->buf.empty()) {
            line.swap(st->buf);
            got = true;
        }
    }
    if (!got) return PROTO_NONE;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return string(ctx, line);
}

PRIM(prim_FdReadAll) {
    ARGS(1, "__fdReadAll:");
    const int fd = static_cast<int>(num(ctx, a[0], "upToEnd"));
    auto st = fdState(fd);
    std::string all;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        while (fill(fd, *st)) {}
        all.swap(st->buf);
    }
    return string(ctx, all);
}

// __fdRead: fd count: n binary: aBoolean — up to n bytes, nil at end.
PRIM(prim_FdRead) {
    ARGS(3, "__fdRead:count:binary:");
    const int fd = static_cast<int>(num(ctx, a[0], "next:"));
    const long long n = num(ctx, a[1], "next:");
    const bool binary = truthy(a[2]);
    auto st = fdState(fd);
    std::string got;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        while (st->buf.size() < static_cast<size_t>(n) && fill(fd, *st)) {}
        const size_t take = std::min(st->buf.size(), static_cast<size_t>(n));
        got = st->buf.substr(0, take);
        st->buf.erase(0, take);
    }
    if (got.empty() && n > 0) return PROTO_NONE;
    return binary ? bytesArray(rt, ctx, got) : string(ctx, got);
}

// __fdReadChars: fd count: n — up to n UTF-8 characters (never a split
// character), nil at end.
PRIM(prim_FdReadChars) {
    ARGS(2, "__fdReadChars:count:");
    const int fd = static_cast<int>(num(ctx, a[0], "next:"));
    const long long n = num(ctx, a[1], "next:");
    auto st = fdState(fd);
    std::string got;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        // Byte length of the first `n` characters, or npos when the buffer
        // does not hold them all yet (a lead byte is any byte that is not
        // 10xxxxxx; its sequence length comes from its high bits).
        auto prefix = [&](const std::string& b) -> size_t {
            size_t i = 0;
            for (long long c = 0; c < n; ++c) {
                if (i >= b.size()) return std::string::npos;
                const unsigned char lead = static_cast<unsigned char>(b[i]);
                const size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3
                                 : (lead >> 3) == 0x1E ? 4 : 1;
                if (i + len > b.size()) return std::string::npos;
                i += len;
            }
            return i;
        };
        size_t take;
        while ((take = prefix(st->buf)) == std::string::npos && fill(fd, *st)) {}
        if (take == std::string::npos) take = st->buf.size();  // end of stream: what is left
        got = st->buf.substr(0, take);
        st->buf.erase(0, take);
    }
    if (got.empty() && n > 0) return PROTO_NONE;
    return string(ctx, got);
}

PRIM(prim_FdAtEnd) {
    ARGS(1, "__fdAtEnd:");
    const int fd = static_cast<int>(num(ctx, a[0], "atEnd"));
    auto st = fdState(fd);
    bool end;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        end = st->buf.empty() && !fill(fd, *st);
    }
    return boolean(end);
}

// __fdWrite: fd data: aStringOrByteArray
PRIM(prim_FdWrite) {
    ARGS(2, "__fdWrite:data:");
    const int fd = static_cast<int>(num(ctx, a[0], "nextPutAll:"));
    const std::string data = a[1] && a[1]->asString(ctx) ? str(ctx, a[1], "nextPutAll:")
                                                         : bytesOf(ctx, a[1], "nextPutAll:");
    if (fd == 1 || fd == 2) {
        // Through the C stream, so the order with Transcript and printNl
        // (which write to stdout) is kept.
        std::FILE* f = fd == 1 ? stdout : stderr;
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        std::fwrite(data.data(), 1, data.size(), f);
        if (fd == 2 || (!data.empty() && data.back() == '\n')) std::fflush(f);
        return r;
    }
    auto st = fdState(fd);
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        rawWrite(fd, *st, data);
    }
    return r;
}

PRIM(prim_FdFlush) {
    ARGS(1, "__fdFlush:");
    const int fd = static_cast<int>(num(ctx, a[0], "flush"));
    if (fd == 1) std::fflush(stdout);
    if (fd == 2) std::fflush(stderr);
    return r;
}

PRIM(prim_FdClose) {
    ARGS(1, "__fdClose:");
    const int fd = static_cast<int>(num(ctx, a[0], "close"));
    if (fd <= 2) { std::fflush(nullptr); return r; }
    std::shared_ptr<FdState> st;
    {
        std::lock_guard<std::mutex> lock(g_fdMutex);
        auto it = g_fds.find(fd);
        if (it != g_fds.end()) { st = it->second; g_fds.erase(it); }
    }
    if (st && st->ssl) {
        proto::ProtoContext::UnmanagedScope out(ctx);
        SSL_shutdown(st->ssl);
        SSL_free(st->ssl);
        st->ssl = nullptr;
    }
    // shutdown wakes a thread blocked in accept or read on this socket,
    // which a bare close does not; it applies to any socket, registered or not.
    struct stat sst{};
    if (::fstat(fd, &sst) == 0 && S_ISSOCK(sst.st_mode)) ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    return r;
}

// __byteSize: aString — its length in UTF-8 bytes (HTTP Content-Length).
PRIM(prim_ByteSize) {
    ARGS(1, "__byteSize:");
    return ctx->fromLong(static_cast<long long>(str(ctx, a[0], "byteSize").size()));
}

PRIM(prim_FdTimeout) {
    ARGS(2, "__fdTimeout:ms:");
    const int fd = static_cast<int>(num(ctx, a[0], "timeout:"));
    fdState(fd)->timeoutMs = a[1] == PROTO_NONE ? -1 : static_cast<int>(num(ctx, a[1], "timeout:"));
    return r;
}

// ------------------------------------------------------------------ files

// __fileOpen: path mode: 'r' | 'w' | 'a' | 'rw'
PRIM(prim_FileOpen) {
    ARGS(2, "__fileOpen:mode:");
    const std::string path = str(ctx, a[0], "open");
    const std::string mode = str(ctx, a[1], "open");
    int flags = O_CLOEXEC;
    if (mode == "r") flags |= O_RDONLY;
    else if (mode == "w") flags |= O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode == "a") flags |= O_WRONLY | O_CREAT | O_APPEND;
    else if (mode == "rw") flags |= O_RDWR | O_CREAT;
    else throw std::runtime_error("open: mode must be r, w, a or rw");
    int fd;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        fd = ::open(path.c_str(), flags, 0644);
    }
    if (fd < 0) fileError(path, errno, "cannot open");
    forgetFd(fd);
    return ctx->fromLong(fd);
}

std::string readWhole(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) fileError(path, errno, "cannot read");
    struct stat st{};
    if (::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) { ::close(fd); fileError(path, EISDIR, "cannot read"); }
    std::string out;
    char chunk[65536];
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { const int e = errno; ::close(fd); fileError(path, e, "cannot read"); }
        if (n == 0) break;
        out.append(chunk, static_cast<size_t>(n));
    }
    ::close(fd);
    return out;
}

void writeWhole(const std::string& path, const std::string& data, bool append) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) fileError(path, errno, "cannot write");
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t w = ::write(fd, data.data() + done, data.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) { const int e = errno; ::close(fd); fileError(path, e, "cannot write"); }
        done += static_cast<size_t>(w);
    }
    ::close(fd);
}

// __fileRead: path binary: aBoolean
PRIM(prim_FileRead) {
    ARGS(2, "__fileRead:binary:");
    const std::string path = str(ctx, a[0], "contents");
    std::string data;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        data = readWhole(path);
    }
    return truthy(a[1]) ? bytesArray(rt, ctx, data) : string(ctx, data);
}

// __fileWrite: path data: aStringOrBytes append: aBoolean
PRIM(prim_FileWrite) {
    ARGS(3, "__fileWrite:data:append:");
    const std::string path = str(ctx, a[0], "contents:");
    const std::string data = a[1] && a[1]->asString(ctx) ? str(ctx, a[1], "contents:")
                                                         : bytesOf(ctx, a[1], "contents:");
    const bool append = truthy(a[2]);
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        writeWhole(path, data, append);
    }
    return r;
}

// __fileStat: path — nil when absent, else
// #(isFile isDirectory size modificationMillis readable writable)
PRIM(prim_FileStat) {
    ARGS(1, "__fileStat:");
    const std::string path = str(ctx, a[0], "exists");
    struct stat st{};
    int rc, readable, writable;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        rc = ::stat(path.c_str(), &st);
        readable = ::access(path.c_str(), R_OK) == 0;
        writable = ::access(path.c_str(), W_OK) == 0;
    }
    if (rc != 0) return PROTO_NONE;
    const long long ms = static_cast<long long>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
    return makeArray(rt, ctx, {boolean(S_ISREG(st.st_mode)), boolean(S_ISDIR(st.st_mode)),
                               ctx->fromLong(static_cast<long long>(st.st_size)), ctx->fromLong(ms),
                               boolean(readable), boolean(writable)});
}

// __fileDelete: path recursive: aBoolean — answers whether something was deleted.
PRIM(prim_FileDelete) {
    ARGS(2, "__fileDelete:recursive:");
    const std::string path = str(ctx, a[0], "delete");
    const bool all = truthy(a[1]);
    bool removed = false;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        try {
            removed = all ? fs::remove_all(path) > 0 : fs::remove(path);
        } catch (const fs::filesystem_error& e) { fsError(e, "cannot delete"); }
    }
    return boolean(removed);
}

// __fileMove: from to: to
PRIM(prim_FileMove) {
    ARGS(2, "__fileMove:to:");
    const std::string from = str(ctx, a[0], "moveTo:"), to = str(ctx, a[1], "moveTo:");
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        try { fs::rename(from, to); } catch (const fs::filesystem_error& e) { fsError(e, "cannot move"); }
    }
    return r;
}

// __fileCopy: from to: to — files and whole directory trees.
PRIM(prim_FileCopy) {
    ARGS(2, "__fileCopy:to:");
    const std::string from = str(ctx, a[0], "copyTo:"), to = str(ctx, a[1], "copyTo:");
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        try {
            fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        } catch (const fs::filesystem_error& e) { fsError(e, "cannot copy"); }
    }
    return r;
}

// __dirCreate: path parents: aBoolean
PRIM(prim_DirCreate) {
    ARGS(2, "__dirCreate:parents:");
    const std::string path = str(ctx, a[0], "createDirectory");
    const bool parents = truthy(a[1]);
    int err = 0;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        if (parents) {
            std::error_code ec;
            fs::create_directories(path, ec);
            if (ec) err = ec.value();
        } else if (::mkdir(path.c_str(), 0755) != 0) {
            err = errno;
        }
    }
    if (err) fileError(path, err, "cannot create directory");
    return r;
}

// __dirList: path — the entry names, sorted.
PRIM(prim_DirList) {
    ARGS(1, "__dirList:");
    const std::string path = str(ctx, a[0], "children");
    std::vector<std::string> names;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        try {
            for (const auto& e : fs::directory_iterator(path)) names.push_back(e.path().filename().string());
        } catch (const fs::filesystem_error& e) { fsError(e, "cannot list"); }
        std::sort(names.begin(), names.end());
    }
    return stringsArray(rt, ctx, names);
}

PRIM(prim_FileAbsolute) {
    ARGS(1, "__fileAbsolute:");
    const std::string path = str(ctx, a[0], "fullName");
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
    if (ec) p = fs::absolute(path);
    std::string s = p.lexically_normal().string();
    if (s.size() > 1 && s.back() == '/') s.pop_back();
    return string(ctx, s);
}

PRIM(prim_TempDir) {
    ARGS(0, "__fileTempDir");
    std::error_code ec;
    return string(ctx, fs::temp_directory_path(ec).string());
}

// --------------------------------------------------------------- processes

struct ChildResult { int status = 0; std::string out, err; };

int exitCodeOf(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// Runs argv with the given input and collects both outputs. Unmanaged.
ChildResult runChild(const std::vector<std::string>& argv, const std::string* input) {
    int inP[2], outP[2], errP[2];
    if (::pipe2(inP, O_CLOEXEC) || ::pipe2(outP, O_CLOEXEC) || ::pipe2(errP, O_CLOEXEC))
        throw ClassedErrorSignal("OSProcessError", std::string("cannot create pipes: ") + std::strerror(errno));
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, inP[0], 0);
    posix_spawn_file_actions_adddup2(&fa, outP[1], 1);
    posix_spawn_file_actions_adddup2(&fa, errP[1], 2);
    std::vector<char*> args;
    for (const std::string& s : argv) args.push_back(const_cast<char*>(s.c_str()));
    args.push_back(nullptr);
    pid_t pid;
    const int rc = ::posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(inP[0]); ::close(outP[1]); ::close(errP[1]);
    if (rc != 0) {
        ::close(inP[1]); ::close(outP[0]); ::close(errP[0]);
        throw ClassedErrorSignal("OSProcessError", "cannot run " + argv[0] + ": " + std::strerror(rc));
    }
    ChildResult res;
    size_t written = 0;
    const std::string empty;
    const std::string& in = input ? *input : empty;
    if (in.empty()) { ::close(inP[1]); inP[1] = -1; }
    else ::fcntl(inP[1], F_SETFL, O_NONBLOCK);
    int outFd = outP[0], errFd = errP[0];
    char chunk[65536];
    while (outFd >= 0 || errFd >= 0 || inP[1] >= 0) {
        pollfd p[3];
        int n = 0, iOut = -1, iErr = -1, iIn = -1;
        if (outFd >= 0) { iOut = n; p[n++] = {outFd, POLLIN, 0}; }
        if (errFd >= 0) { iErr = n; p[n++] = {errFd, POLLIN, 0}; }
        if (inP[1] >= 0) { iIn = n; p[n++] = {inP[1], POLLOUT, 0}; }
        if (::poll(p, n, -1) < 0) { if (errno == EINTR) continue; break; }
        auto drain = [&](int idx, int& fd, std::string& dst) {
            if (idx < 0 || !(p[idx].revents & (POLLIN | POLLHUP | POLLERR))) return;
            const ssize_t k = ::read(fd, chunk, sizeof chunk);
            if (k > 0) dst.append(chunk, static_cast<size_t>(k));
            else if (k == 0 || errno != EINTR) { ::close(fd); fd = -1; }
        };
        drain(iOut, outFd, res.out);
        drain(iErr, errFd, res.err);
        if (iIn >= 0 && (p[iIn].revents & (POLLOUT | POLLERR | POLLHUP))) {
            const ssize_t w = ::write(inP[1], in.data() + written, in.size() - written);
            if (w > 0) written += static_cast<size_t>(w);
            if (w < 0 && errno != EAGAIN && errno != EINTR) written = in.size();
            if (written >= in.size()) { ::close(inP[1]); inP[1] = -1; }
        }
    }
    while (::waitpid(pid, &res.status, 0) < 0 && errno == EINTR) {}
    return res;
}

// __osRun: argvArray input: aStringOrNil — #(exitCode output errorOutput)
PRIM(prim_Run) {
    ARGS(2, "__osRun:input:");
    const std::vector<std::string> argv = stringArray(ctx, a[0], "run:arguments:");
    if (argv.empty()) throw std::runtime_error("run:arguments: needs a command");
    std::string input;
    const bool hasInput = a[1] && a[1] != PROTO_NONE;
    if (hasInput) input = str(ctx, a[1], "input:");
    ChildResult res;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        res = runChild(argv, hasInput ? &input : nullptr);
    }
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {ctx->fromLong(exitCodeOf(res.status)), string(ctx, res.out), string(ctx, res.err)});
}

// __osSpawn: argvArray — starts a child that shares our standard streams; answers its pid.
PRIM(prim_Spawn) {
    ARGS(1, "__osSpawn:");
    const std::vector<std::string> argv = stringArray(ctx, a[0], "spawn:arguments:");
    if (argv.empty()) throw std::runtime_error("spawn:arguments: needs a command");
    std::vector<char*> args;
    for (const std::string& s : argv) args.push_back(const_cast<char*>(s.c_str()));
    args.push_back(nullptr);
    pid_t pid;
    std::fflush(nullptr);
    const int rc = ::posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ);
    if (rc != 0) throw ClassedErrorSignal("OSProcessError", "cannot run " + argv[0] + ": " + std::strerror(rc));
    return ctx->fromLong(pid);
}

PRIM(prim_WaitPid) {
    ARGS(1, "__osWaitPid:");
    const pid_t pid = static_cast<pid_t>(num(ctx, a[0], "waitFor:"));
    int status = 0, rc;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        while ((rc = ::waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    }
    if (rc < 0) throw ClassedErrorSignal("OSProcessError", std::string("waitFor: ") + std::strerror(errno));
    return ctx->fromLong(exitCodeOf(status));
}

PRIM(prim_Kill) {
    ARGS(2, "__osKill:signal:");
    const pid_t pid = static_cast<pid_t>(num(ctx, a[0], "kill:"));
    const int sig = static_cast<int>(num(ctx, a[1], "kill:"));
    if (::kill(pid, sig) != 0) throw ClassedErrorSignal("OSProcessError", std::string("kill: ") + std::strerror(errno));
    return r;
}

// ---------------------------------------------------------------- network

// __tcpConnect: host port: p timeout: msOrNil — answers a descriptor.
PRIM(prim_TcpConnect) {
    ARGS(3, "__tcpConnect:port:timeout:");
    const std::string host = str(ctx, a[0], "connectTo:");
    const int port = static_cast<int>(num(ctx, a[1], "connectTo:port:"));
    const int timeoutMs = a[2] == PROTO_NONE ? -1 : static_cast<int>(num(ctx, a[2], "timeout:"));
    int fd = -1;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        AddrList addrs;
        resolve(host, port, SOCK_STREAM, false, addrs);
        int lastErr = ECONNREFUSED;
        for (addrinfo* ai = addrs.head; ai; ai = ai->ai_next) {
            fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, ai->ai_protocol);
            if (fd < 0) { lastErr = errno; continue; }
            int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
            if (rc != 0 && errno == EINPROGRESS) {
                pollfd p{fd, POLLOUT, 0};
                int pr;
                while ((pr = ::poll(&p, 1, timeoutMs)) < 0 && errno == EINTR) {}
                if (pr == 0) { ::close(fd); fd = -1; lastErr = ETIMEDOUT; continue; }
                int soErr = 0; socklen_t len = sizeof soErr;
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len);
                rc = soErr ? -1 : 0;
                if (soErr) errno = soErr;
            }
            if (rc == 0) break;
            lastErr = errno;
            ::close(fd);
            fd = -1;
        }
        if (fd < 0) netError(lastErr, "cannot connect to " + host + ":" + std::to_string(port));
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    forgetFd(fd);
    return ctx->fromLong(fd);
}

// __tcpListen: host port: p backlog: n — answers a descriptor.
PRIM(prim_TcpListen) {
    ARGS(3, "__tcpListen:port:backlog:");
    const std::string host = str(ctx, a[0], "listenOn:");
    const int port = static_cast<int>(num(ctx, a[1], "listenOn:"));
    const int backlog = static_cast<int>(num(ctx, a[2], "listenOn:"));
    int fd = -1, lastErr = EADDRNOTAVAIL;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        AddrList addrs;
        resolve(host, port, SOCK_STREAM, true, addrs);
        for (addrinfo* ai = addrs.head; ai; ai = ai->ai_next) {
            fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
            if (fd < 0) { lastErr = errno; continue; }
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && ::listen(fd, backlog) == 0) break;
            lastErr = errno;
            ::close(fd);
            fd = -1;
        }
    }
    if (fd < 0) netError(lastErr, "cannot listen on port " + std::to_string(port));
    forgetFd(fd);
    return ctx->fromLong(fd);
}

// __tcpAccept: fd timeout: msOrNil — a connected descriptor, or nil on timeout.
PRIM(prim_TcpAccept) {
    ARGS(2, "__tcpAccept:timeout:");
    const int fd = static_cast<int>(num(ctx, a[0], "accept"));
    const int timeoutMs = a[1] == PROTO_NONE ? -1 : static_cast<int>(num(ctx, a[1], "acceptTimeout:"));
    int c = -1, err = 0;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        if (!waitReady(fd, POLLIN, timeoutMs)) return PROTO_NONE;
        while ((c = ::accept4(fd, nullptr, nullptr, SOCK_CLOEXEC)) < 0 && errno == EINTR) {}
        if (c < 0) err = errno;
    }
    if (c < 0) netError(err, "accept");
    int one = 1;
    ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    forgetFd(c);
    return ctx->fromLong(c);
}

// __sockName: fd peer: aBoolean — #(host port)
PRIM(prim_SockName) {
    ARGS(2, "__sockName:peer:");
    const int fd = static_cast<int>(num(ctx, a[0], "port"));
    sockaddr_storage ss{};
    socklen_t len = sizeof ss;
    const int rc = truthy(a[1]) ? ::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len)
                                : ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len);
    if (rc != 0) netError(errno, "address of socket");
    int port = 0;
    const std::string host = addrText(reinterpret_cast<sockaddr*>(&ss), &port);
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {string(ctx, host), ctx->fromLong(port)});
}

// __tlsConnect: fd host: name verify: aBoolean — upgrades the socket to TLS.
PRIM(prim_TlsConnect) {
    ARGS(3, "__tlsConnect:host:verify:");
    const int fd = static_cast<int>(num(ctx, a[0], "tlsHost:"));
    const std::string host = str(ctx, a[1], "tlsHost:");
    const bool verify = truthy(a[2]);
    auto st = fdState(fd);
    std::string failure;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        SSL* ssl = SSL_new(tlsContext(verify));
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host.c_str());
        if (verify) SSL_set1_host(ssl, host.c_str());
        ERR_clear_error();
        if (SSL_connect(ssl) != 1) {
            const long v = SSL_get_verify_result(ssl);
            failure = v != X509_V_OK ? std::string("TLS certificate: ") + X509_verify_cert_error_string(v)
                                     : tlsErrorText();
            SSL_free(ssl);
        } else {
            st->ssl = ssl;
        }
    }
    if (!failure.empty()) throw ClassedErrorSignal("NetworkError", failure + " (" + host + ")");
    return r;
}

// __udpBind: host port: p — answers a descriptor.
PRIM(prim_UdpBind) {
    ARGS(2, "__udpBind:port:");
    const std::string host = str(ctx, a[0], "bindTo:");
    const int port = static_cast<int>(num(ctx, a[1], "bindTo:"));
    int fd = -1, lastErr = EADDRNOTAVAIL;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        AddrList addrs;
        resolve(host.empty() ? "0.0.0.0" : host, port, SOCK_DGRAM, true, addrs);
        for (addrinfo* ai = addrs.head; ai; ai = ai->ai_next) {
            fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
            if (fd < 0) { lastErr = errno; continue; }
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
            if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
            lastErr = errno;
            ::close(fd);
            fd = -1;
        }
    }
    if (fd < 0) netError(lastErr, "cannot bind UDP port " + std::to_string(port));
    forgetFd(fd);
    return ctx->fromLong(fd);
}

// __udpSend: fd to: host port: p data: aStringOrBytes
PRIM(prim_UdpSend) {
    ARGS(4, "__udpSend:to:port:data:");
    const int fd = static_cast<int>(num(ctx, a[0], "send:"));
    const std::string host = str(ctx, a[1], "send:to:port:");
    const int port = static_cast<int>(num(ctx, a[2], "send:to:port:"));
    const std::string data = a[3] && a[3]->asString(ctx) ? str(ctx, a[3], "send:") : bytesOf(ctx, a[3], "send:");
    int err = 0;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        AddrList addrs;
        resolve(host, port, SOCK_DGRAM, false, addrs);
        if (::sendto(fd, data.data(), data.size(), MSG_NOSIGNAL, addrs.head->ai_addr, addrs.head->ai_addrlen) < 0)
            err = errno;
    }
    if (err) netError(err, "send to " + host);
    return r;
}

// __udpReceive: fd timeout: msOrNil binary: aBoolean — #(data host port) or nil on timeout.
PRIM(prim_UdpReceive) {
    ARGS(3, "__udpReceive:timeout:binary:");
    const int fd = static_cast<int>(num(ctx, a[0], "receive"));
    const int timeoutMs = a[1] == PROTO_NONE ? -1 : static_cast<int>(num(ctx, a[1], "receiveTimeout:"));
    const bool binary = truthy(a[2]);
    std::string data;
    sockaddr_storage ss{};
    int err = 0;
    {
        BlockingIO blocking(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        if (!waitReady(fd, POLLIN, timeoutMs)) return PROTO_NONE;
        data.resize(65536);
        socklen_t len = sizeof ss;
        ssize_t n;
        while ((n = ::recvfrom(fd, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&ss), &len)) < 0
               && errno == EINTR) {}
        if (n < 0) err = errno; else data.resize(static_cast<size_t>(n));
    }
    if (err) netError(err, "receive");
    int port = 0;
    const std::string host = addrText(reinterpret_cast<sockaddr*>(&ss), &port);
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {binary ? bytesArray(rt, ctx, data) : string(ctx, data),
                               string(ctx, host), ctx->fromLong(port)});
}

#undef ARGS
#undef PRIM

} // anon

void setProgramArguments(const std::string& programPath, const std::vector<std::string>& args) {
    g_programPath = programPath;
    g_programArgs = args;
}

void installIoPrimitives(STRuntime& rt) {
    auto& reg = rt.registry();
    const proto::ProtoObject* o = rt.bootstrap().objectProto;
    struct Entry { const char* sel; PrimFn fn; };
    const Entry entries[] = {
        {"__osArguments", prim_Arguments},       {"__osProgramPath", prim_ProgramPath},
        {"__osGetenv:", prim_Getenv},            {"__osSetenv:to:", prim_Setenv},
        {"__osEnvironment", prim_Environment},   {"__osExit:", prim_Exit},
        {"__osPid", prim_Pid},                   {"__osHostName", prim_HostName},
        {"__osPlatform", prim_Platform},         {"__osCwd", prim_Cwd},
        {"__osChdir:", prim_Chdir},              {"__osRun:input:", prim_Run},
        {"__osSpawn:", prim_Spawn},              {"__osWaitPid:", prim_WaitPid},
        {"__osKill:signal:", prim_Kill},
        {"__fdReadLine:", prim_FdReadLine},      {"__fdReadAll:", prim_FdReadAll},
        {"__fdRead:count:binary:", prim_FdRead}, {"__fdAtEnd:", prim_FdAtEnd},
        {"__fdReadChars:count:", prim_FdReadChars},
        {"__fdWrite:data:", prim_FdWrite},       {"__fdFlush:", prim_FdFlush},
        {"__fdClose:", prim_FdClose},            {"__fdTimeout:ms:", prim_FdTimeout},
        {"__byteSize:", prim_ByteSize},
        {"__fileOpen:mode:", prim_FileOpen},     {"__fileRead:binary:", prim_FileRead},
        {"__fileWrite:data:append:", prim_FileWrite}, {"__fileStat:", prim_FileStat},
        {"__fileDelete:recursive:", prim_FileDelete}, {"__fileMove:to:", prim_FileMove},
        {"__fileCopy:to:", prim_FileCopy},       {"__dirCreate:parents:", prim_DirCreate},
        {"__dirList:", prim_DirList},            {"__fileAbsolute:", prim_FileAbsolute},
        {"__fileTempDir", prim_TempDir},
        {"__tcpConnect:port:timeout:", prim_TcpConnect}, {"__tcpListen:port:backlog:", prim_TcpListen},
        {"__tcpAccept:timeout:", prim_TcpAccept}, {"__sockName:peer:", prim_SockName},
        {"__tlsConnect:host:verify:", prim_TlsConnect},
        {"__udpBind:port:", prim_UdpBind},       {"__udpSend:to:port:data:", prim_UdpSend},
        {"__udpReceive:timeout:binary:", prim_UdpReceive},
    };
    for (const Entry& e : entries) bindPrimitive(rt, o, e.sel, reg.registerPrim(e.fn));
}

} // namespace protoST
