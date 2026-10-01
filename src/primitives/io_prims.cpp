#include "protoST/STRuntime.h"
#include "protoST/primitives.h"
#include "runtime/Bootstrap.h"
#include "runtime/TransientPin.h"
#include "runtime/ZeroDivideSignal.h"
#include "protoCore.h"

#include <protoio/error.h>
#include <protoio/file.h>
#include <protoio/http.h>
#include <protoio/net.h>
#include <protoio/process.h>
#include <protoio/stream.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protoST {

// Input and output (docs/superpowers/specs/2026-09-29-io-design.md).
//
// Low-level primitives bound on `objectProto`; the classes a program uses
// (Stdio, FileReference, OSProcess in lib/kernel/io.st; Socket, ServerSocket,
// UDPSocket in lib/net.st; HTTPClient, HTTPServer in lib/http.st) are protoST
// code on top of them. Streams are integers: POSIX file descriptors.
//
// The POSIX layer itself (buffered descriptors, SIGPIPE handling, TLS, child
// processes, sockets, the chunked-body reader) lives in the protoIO library,
// shared with the other protoCore runtimes. This file only binds it:
//   * arguments are copied into C++ values BEFORE anything blocks;
//   * every protoIO call that can block runs inside a
//     ProtoContext::UnmanagedScope and touches no protoCore object there, so a
//     thread blocked on I/O never holds up a collection; calls that can wait
//     on a peer, a child or a terminal also account themselves to the worker
//     pool (BlockingIO), which may add a worker while this one waits;
//   * protoCore values are built only after the blocking part returns;
//   * protoio::Error becomes a classed, catchable protoST error
//     (FileDoesNotExist, ConnectionRefused, ..., declared in lib/kernel/io.st),
//     raised after the unmanaged scope has been left.

namespace {

using PO = proto::ProtoObject;
using Kind = protoio::Error::Kind;

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

int fdArg(proto::ProtoContext* ctx, const PO* v, const char* who) { return static_cast<int>(num(ctx, v, who)); }

// A timeout in milliseconds, nil meaning no limit (-1).
int timeoutArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    return v == PROTO_NONE ? -1 : static_cast<int>(num(ctx, v, who));
}

// The count argument of next:, nextBytes: and the like.
size_t countArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    const long long n = num(ctx, v, who);
    if (n < 0) throw std::runtime_error(std::string(who) + ": the count must not be negative");
    return static_cast<size_t>(n);
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
    const proto::proto_ulong n = l->getSize(ctx);
    out.reserve(n);
    for (proto::proto_ulong i = 0; i < n; ++i) out.push_back(l->getAt(ctx, static_cast<int>(i)));
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

// A String argument as its UTF-8 bytes, or a byte Array as its bytes.
std::string dataArg(proto::ProtoContext* ctx, const PO* v, const char* who) {
    return v && v->asString(ctx) ? str(ctx, v, who) : bytesOf(ctx, v, who);
}

// ------------------------------------------------------------------ errors

// The protoST error class of each protoio::Error kind. InvalidArgument (a
// value the library refuses) is the generic error the primitives raise for a
// bad argument.
[[noreturn]] void raise(const protoio::Error& e) {
    switch (e.kind) {
        case Kind::FileNotFound: throw ClassedErrorSignal("FileDoesNotExist", e.what());
        case Kind::FileExists: throw ClassedErrorSignal("FileAlreadyExists", e.what());
        case Kind::FileSystem: throw ClassedErrorSignal("FileSystemError", e.what());
        case Kind::ConnectionRefused: throw ClassedErrorSignal("ConnectionRefused", e.what());
        case Kind::ConnectionTimedOut: throw ClassedErrorSignal("ConnectionTimedOut", e.what());
        case Kind::NameLookup: throw ClassedErrorSignal("NameLookupFailure", e.what());
        case Kind::Network: throw ClassedErrorSignal("NetworkError", e.what());
        case Kind::Process: throw ClassedErrorSignal("OSProcessError", e.what());
        case Kind::LineTooLong: throw ClassedErrorSignal("LineTooLong", e.what());
        case Kind::BodyTooLarge: throw ClassedErrorSignal("BodyTooLarge", e.what());
        case Kind::InvalidArgument: break;
    }
    throw std::runtime_error(e.what());
}

// ---------------------------------------------------------------- brackets
//
// Each runs `f` (which calls protoIO with C++ values only) and answers its
// result. A protoio::Error is caught after the scopes have been left, so the
// protoST error is raised with the context managed again.

// Accounts a blocking call on a pool worker, so the scheduler can add a
// worker while this one waits (STRuntime::enterBlockingIO). Constructed
// before the UnmanagedScope: adding a worker allocates.
struct BlockingIO {
    STRuntime& rt;
    bool active;
    BlockingIO(STRuntime& r, proto::ProtoContext* ctx) : rt(r), active(r.enterBlockingIO(ctx)) {}
    ~BlockingIO() { if (active) rt.leaveBlockingIO(); }
    BlockingIO(const BlockingIO&) = delete;
    BlockingIO& operator=(const BlockingIO&) = delete;
};

// For a call that can wait without bound: on a peer, a child process, a
// terminal or a pipe.
template <typename F>
auto blocking(STRuntime& rt, proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        BlockingIO accounted(rt, ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

// For a call that can block only briefly (the local file system, binding a
// socket): out of the collector's quorum, without growing the worker pool.
template <typename F>
auto unmanaged(proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

// For a call that does not block.
template <typename F>
auto immediate(F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

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
    const std::optional<std::string> v = protoio::process::getenv(str(ctx, a[0], "getenv:"));
    return v ? string(ctx, *v) : PROTO_NONE;
}

PRIM(prim_Setenv) {
    ARGS(2, "__osSetenv:to:");
    const std::string k = str(ctx, a[0], "setenv:to:");
    std::optional<std::string> v;
    if (a[1] != PROTO_NONE) v = str(ctx, a[1], "setenv:to:");
    immediate([&] { protoio::process::setenv(k, v); });
    return r;
}

// Each variable as 'NAME=value' (SmalltalkImage>>environment splits them).
PRIM(prim_Environment) {
    ARGS(0, "__osEnvironment");
    std::vector<std::string> v;
    for (const auto& [name, value] : protoio::process::environment()) v.push_back(name + "=" + value);
    return stringsArray(rt, ctx, v);
}

PRIM(prim_Exit) {
    ARGS(1, "__osExit:");
    protoio::process::exit(static_cast<int>(num(ctx, a[0], "exit:")));
}

PRIM(prim_Pid) { ARGS(0, "__osPid"); return ctx->fromLong(protoio::process::pid()); }

PRIM(prim_HostName) { ARGS(0, "__osHostName"); return string(ctx, protoio::process::hostName()); }

PRIM(prim_Platform) { ARGS(0, "__osPlatform"); return string(ctx, protoio::process::platform()); }

PRIM(prim_Cwd) {
    ARGS(0, "__osCwd");
    return string(ctx, immediate([] { return protoio::file::cwd(); }));
}

PRIM(prim_Chdir) {
    ARGS(1, "__osChdir:");
    const std::string p = str(ctx, a[0], "changeDirectory:");
    immediate([&] { protoio::file::chdir(p); });
    return r;
}

// ------------------------------------------------------------ descriptors

// A line without its end (LF or CRLF), or nil at end of stream. A line
// longer than `max` bytes (0: no limit) raises LineTooLong, leaving the
// stream where it was.
const PO* readLine(STRuntime& rt, proto::ProtoContext* ctx, int fd, size_t max) {
    const std::optional<std::string> line = blocking(rt, ctx, [&] { return protoio::readLine(fd, max); });
    return line ? string(ctx, *line) : PROTO_NONE;
}

PRIM(prim_FdReadLine) {
    ARGS(1, "__fdReadLine:");
    return readLine(rt, ctx, fdArg(ctx, a[0], "nextLine"), 0);
}

// __fdReadLine: fd max: bytes — nextLine, refusing lines over `bytes`.
PRIM(prim_FdReadLineMax) {
    ARGS(2, "__fdReadLine:max:");
    return readLine(rt, ctx, fdArg(ctx, a[0], "nextLineMax:"), countArg(ctx, a[1], "nextLineMax:"));
}

PRIM(prim_FdReadAll) {
    ARGS(1, "__fdReadAll:");
    const int fd = fdArg(ctx, a[0], "upToEnd");
    return string(ctx, blocking(rt, ctx, [&] { return protoio::readAll(fd); }));
}

// __fdRead: fd count: n binary: aBoolean — up to n bytes, nil at end.
PRIM(prim_FdRead) {
    ARGS(3, "__fdRead:count:binary:");
    const int fd = fdArg(ctx, a[0], "next:");
    const size_t n = countArg(ctx, a[1], "nextBytes:");
    const bool binary = truthy(a[2]);
    const std::optional<std::string> got = blocking(rt, ctx, [&] { return protoio::readBytes(fd, n); });
    if (!got) return PROTO_NONE;
    return binary ? bytesArray(rt, ctx, *got) : string(ctx, *got);
}

// __fdReadChars: fd count: n — up to n UTF-8 characters (never a split
// character), nil at end.
PRIM(prim_FdReadChars) {
    ARGS(2, "__fdReadChars:count:");
    const int fd = fdArg(ctx, a[0], "next:");
    const size_t n = countArg(ctx, a[1], "next:");
    const std::optional<std::string> got = blocking(rt, ctx, [&] { return protoio::readChars(fd, n); });
    return got ? string(ctx, *got) : PROTO_NONE;
}

// __httpReadChunked: fd max: bytes — an HTTP chunked body, decoded as bytes
// and answered as one String (a character split across two chunks arrives
// whole). Trailers are skipped. A body over `max` bytes (0: no limit) raises
// BodyTooLarge; a malformed chunk size raises NetworkError.
PRIM(prim_HttpReadChunked) {
    ARGS(2, "__httpReadChunked:max:");
    const int fd = fdArg(ctx, a[0], "readChunked");
    const size_t max = countArg(ctx, a[1], "readChunked");
    const std::string body = blocking(rt, ctx, [&] {
        return protoio::http::readBody(fd, {{"transfer-encoding", "chunked"}}, max);
    });
    return string(ctx, body);
}

PRIM(prim_FdAtEnd) {
    ARGS(1, "__fdAtEnd:");
    const int fd = fdArg(ctx, a[0], "atEnd");
    return boolean(blocking(rt, ctx, [&] { return protoio::atEnd(fd); }));
}

// __fdWrite: fd data: aStringOrByteArray. Descriptors 1 and 2 go through the
// C streams, so the order with Transcript and printNl is kept.
PRIM(prim_FdWrite) {
    ARGS(2, "__fdWrite:data:");
    const int fd = fdArg(ctx, a[0], "nextPutAll:");
    const std::string data = dataArg(ctx, a[1], "nextPutAll:");
    blocking(rt, ctx, [&] { protoio::write(fd, data); });
    return r;
}

PRIM(prim_FdFlush) {
    ARGS(1, "__fdFlush:");
    protoio::flush(fdArg(ctx, a[0], "flush"));
    return r;
}

// Closing shuts a socket down (waking a thread blocked on it) and never
// waits: the number is released when its last user returns.
PRIM(prim_FdClose) {
    ARGS(1, "__fdClose:");
    protoio::close(fdArg(ctx, a[0], "close"));
    return r;
}

// __byteSize: aString — its length in UTF-8 bytes (HTTP Content-Length).
PRIM(prim_ByteSize) {
    ARGS(1, "__byteSize:");
    return ctx->fromLong(static_cast<long long>(str(ctx, a[0], "byteSize").size()));
}

// __percentDecode: aString — %XX sequences decoded as bytes, the result read
// as UTF-8 ('%C3%A9' is one character). '+' is left alone: it means a space
// only in a query string, which HTTP class>>parseQuery: handles.
PRIM(prim_PercentDecode) {
    ARGS(1, "__percentDecode:");
    return string(ctx, protoio::http::percentDecode(str(ctx, a[0], "percentDecode:")));
}

PRIM(prim_FdTimeout) {
    ARGS(2, "__fdTimeout:ms:");
    const int fd = fdArg(ctx, a[0], "timeout:");
    protoio::setTimeout(fd, timeoutArg(ctx, a[1], "timeout:"));
    return r;
}

// ------------------------------------------------------------------ files

// __fileOpen: path mode: 'r' | 'w' | 'a' | 'rw'
PRIM(prim_FileOpen) {
    ARGS(2, "__fileOpen:mode:");
    const std::string path = str(ctx, a[0], "open");
    const std::string mode = str(ctx, a[1], "open");
    protoio::file::Mode m;
    if (mode == "r") m = protoio::file::Mode::Read;
    else if (mode == "w") m = protoio::file::Mode::Write;
    else if (mode == "a") m = protoio::file::Mode::Append;
    else if (mode == "rw") m = protoio::file::Mode::ReadWrite;
    else throw std::runtime_error("open: mode must be r, w, a or rw");
    // Opening a FIFO waits for its other end.
    return ctx->fromLong(blocking(rt, ctx, [&] { return protoio::file::open(path, m); }));
}

// __fileRead: path binary: aBoolean
PRIM(prim_FileRead) {
    ARGS(2, "__fileRead:binary:");
    const std::string path = str(ctx, a[0], "contents");
    const std::string data = unmanaged(ctx, [&] { return protoio::file::read(path); });
    return truthy(a[1]) ? bytesArray(rt, ctx, data) : string(ctx, data);
}

// __fileWrite: path data: aStringOrBytes append: aBoolean
PRIM(prim_FileWrite) {
    ARGS(3, "__fileWrite:data:append:");
    const std::string path = str(ctx, a[0], "contents:");
    const std::string data = dataArg(ctx, a[1], "contents:");
    const bool append = truthy(a[2]);
    unmanaged(ctx, [&] {
        if (append) protoio::file::append(path, data);
        else protoio::file::write(path, data);
    });
    return r;
}

// __fileStat: path — nil when absent, else
// #(isFile isDirectory size modificationMillis readable writable)
PRIM(prim_FileStat) {
    ARGS(1, "__fileStat:");
    const std::string path = str(ctx, a[0], "exists");
    const std::optional<protoio::file::Stat> st = unmanaged(ctx, [&] { return protoio::file::stat(path); });
    if (!st) return PROTO_NONE;
    return makeArray(rt, ctx, {boolean(st->isFile), boolean(st->isDirectory),
                               ctx->fromLong(static_cast<long long>(st->size)),
                               ctx->fromLong(static_cast<long long>(st->modifiedMs)),
                               boolean(st->readable), boolean(st->writable)});
}

// __fileDelete: path recursive: aBoolean — answers whether something was deleted.
PRIM(prim_FileDelete) {
    ARGS(2, "__fileDelete:recursive:");
    const std::string path = str(ctx, a[0], "delete");
    const bool all = truthy(a[1]);
    return boolean(unmanaged(ctx, [&] { return protoio::file::remove(path, all); }));
}

// __fileMove: from to: to
PRIM(prim_FileMove) {
    ARGS(2, "__fileMove:to:");
    const std::string from = str(ctx, a[0], "moveTo:"), to = str(ctx, a[1], "moveTo:");
    unmanaged(ctx, [&] { protoio::file::move(from, to); });
    return r;
}

// __fileCopy: from to: to — files and whole directory trees.
PRIM(prim_FileCopy) {
    ARGS(2, "__fileCopy:to:");
    const std::string from = str(ctx, a[0], "copyTo:"), to = str(ctx, a[1], "copyTo:");
    unmanaged(ctx, [&] { protoio::file::copy(from, to); });
    return r;
}

// __dirCreate: path parents: aBoolean
PRIM(prim_DirCreate) {
    ARGS(2, "__dirCreate:parents:");
    const std::string path = str(ctx, a[0], "createDirectory");
    const bool parents = truthy(a[1]);
    unmanaged(ctx, [&] { protoio::file::mkdir(path, parents); });
    return r;
}

// __dirList: path — the entry names, sorted.
PRIM(prim_DirList) {
    ARGS(1, "__dirList:");
    const std::string path = str(ctx, a[0], "children");
    return stringsArray(rt, ctx, unmanaged(ctx, [&] { return protoio::file::list(path); }));
}

// Lexical only: `..` and `.` are folded, symbolic links are kept, as Pharo's
// fullName does.
PRIM(prim_FileAbsolute) {
    ARGS(1, "__fileAbsolute:");
    const std::string path = str(ctx, a[0], "fullName");
    return string(ctx, immediate([&] { return protoio::file::absolute(path); }));
}

PRIM(prim_TempDir) { ARGS(0, "__fileTempDir"); return string(ctx, protoio::file::tempDir()); }

// --------------------------------------------------------------- processes

// __osRun: argvArray input: aStringOrNil — #(exitCode output errorOutput)
PRIM(prim_Run) {
    ARGS(2, "__osRun:input:");
    const std::vector<std::string> argv = stringArray(ctx, a[0], "run:arguments:");
    if (argv.empty()) throw std::runtime_error("run:arguments: needs a command");
    std::optional<std::string> input;
    if (a[1] && a[1] != PROTO_NONE) input = str(ctx, a[1], "input:");
    const protoio::process::RunResult res = blocking(rt, ctx, [&] { return protoio::process::run(argv, input); });
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {ctx->fromLong(res.exitCode), string(ctx, res.out), string(ctx, res.err)});
}

// __osSpawn: argvArray — starts a child that shares our standard streams; answers its pid.
PRIM(prim_Spawn) {
    ARGS(1, "__osSpawn:");
    const std::vector<std::string> argv = stringArray(ctx, a[0], "spawn:arguments:");
    if (argv.empty()) throw std::runtime_error("spawn:arguments: needs a command");
    return ctx->fromLong(immediate([&] { return protoio::process::spawn(argv); }));
}

PRIM(prim_WaitPid) {
    ARGS(1, "__osWaitPid:");
    const int pid = static_cast<int>(num(ctx, a[0], "waitFor:"));
    return ctx->fromLong(blocking(rt, ctx, [&] { return protoio::process::wait(pid); }));
}

PRIM(prim_Kill) {
    ARGS(2, "__osKill:signal:");
    const int pid = static_cast<int>(num(ctx, a[0], "kill:"));
    const int sig = static_cast<int>(num(ctx, a[1], "kill:"));
    immediate([&] { protoio::process::kill(pid, sig); });
    return r;
}

// ---------------------------------------------------------------- network

// __tcpConnect: host port: p timeout: msOrNil — answers a descriptor.
PRIM(prim_TcpConnect) {
    ARGS(3, "__tcpConnect:port:timeout:");
    const std::string host = str(ctx, a[0], "connectTo:");
    const int port = static_cast<int>(num(ctx, a[1], "connectTo:port:"));
    const int timeoutMs = timeoutArg(ctx, a[2], "timeout:");
    return ctx->fromLong(blocking(rt, ctx, [&] { return protoio::net::tcpConnect(host, port, timeoutMs); }));
}

// __tcpListen: host port: p backlog: n — answers a descriptor.
PRIM(prim_TcpListen) {
    ARGS(3, "__tcpListen:port:backlog:");
    const std::string host = str(ctx, a[0], "listenOn:");
    const int port = static_cast<int>(num(ctx, a[1], "listenOn:"));
    const int backlog = static_cast<int>(num(ctx, a[2], "listenOn:"));
    return ctx->fromLong(unmanaged(ctx, [&] { return protoio::net::tcpListen(host, port, backlog); }));
}

// __tcpAccept: fd timeout: msOrNil — a connected descriptor, or nil on
// timeout or when the listener was closed meanwhile.
PRIM(prim_TcpAccept) {
    ARGS(2, "__tcpAccept:timeout:");
    const int fd = fdArg(ctx, a[0], "accept");
    const int timeoutMs = timeoutArg(ctx, a[1], "acceptTimeout:");
    const std::optional<int> c = blocking(rt, ctx, [&] { return protoio::net::tcpAccept(fd, timeoutMs); });
    return c ? ctx->fromLong(*c) : PROTO_NONE;
}

// __sockName: fd peer: aBoolean — #(host port)
PRIM(prim_SockName) {
    ARGS(2, "__sockName:peer:");
    const int fd = fdArg(ctx, a[0], "port");
    const bool peer = truthy(a[1]);
    const protoio::net::Address addr =
        immediate([&] { return peer ? protoio::net::peerName(fd) : protoio::net::sockName(fd); });
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {string(ctx, addr.host), ctx->fromLong(addr.port)});
}

// __tlsConnect: fd host: name verify: aBoolean — upgrades the socket to TLS.
// The handshake, like every later TLS read and write, waits in poll bounded
// by the socket's timeout.
PRIM(prim_TlsConnect) {
    ARGS(3, "__tlsConnect:host:verify:");
    const int fd = fdArg(ctx, a[0], "tlsHost:");
    const std::string host = str(ctx, a[1], "tlsHost:");
    const bool verify = truthy(a[2]);
    blocking(rt, ctx, [&] { protoio::net::tlsConnect(fd, host, verify); });
    return r;
}

// __udpBind: host port: p — answers a descriptor.
PRIM(prim_UdpBind) {
    ARGS(2, "__udpBind:port:");
    const std::string host = str(ctx, a[0], "bindTo:");
    const int port = static_cast<int>(num(ctx, a[1], "bindTo:"));
    return ctx->fromLong(unmanaged(ctx, [&] { return protoio::net::udpBind(host, port); }));
}

// __udpSend: fd to: host port: p data: aStringOrBytes
PRIM(prim_UdpSend) {
    ARGS(4, "__udpSend:to:port:data:");
    const int fd = fdArg(ctx, a[0], "send:");
    const std::string host = str(ctx, a[1], "send:to:port:");
    const int port = static_cast<int>(num(ctx, a[2], "send:to:port:"));
    const std::string data = dataArg(ctx, a[3], "send:");
    unmanaged(ctx, [&] { protoio::net::udpSend(fd, host, port, data); });
    return r;
}

// __udpReceive: fd timeout: msOrNil binary: aBoolean — #(data host port), or
// nil on timeout or when the socket was closed meanwhile.
PRIM(prim_UdpReceive) {
    ARGS(3, "__udpReceive:timeout:binary:");
    const int fd = fdArg(ctx, a[0], "receive");
    const int timeoutMs = timeoutArg(ctx, a[1], "receiveTimeout:");
    const bool binary = truthy(a[2]);
    const std::optional<protoio::net::Datagram> d =
        blocking(rt, ctx, [&] { return protoio::net::udpReceive(fd, timeoutMs); });
    if (!d) return PROTO_NONE;
    proto::ProtoContext::CriticalSection cs(ctx);
    return makeArray(rt, ctx, {binary ? bytesArray(rt, ctx, d->data) : string(ctx, d->data),
                               string(ctx, d->host), ctx->fromLong(d->port)});
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
        {"__fdReadLine:", prim_FdReadLine},      {"__fdReadLine:max:", prim_FdReadLineMax},
        {"__fdReadAll:", prim_FdReadAll},        {"__httpReadChunked:max:", prim_HttpReadChunked},
        {"__fdRead:count:binary:", prim_FdRead}, {"__fdAtEnd:", prim_FdAtEnd},
        {"__fdReadChars:count:", prim_FdReadChars},
        {"__fdWrite:data:", prim_FdWrite},       {"__fdFlush:", prim_FdFlush},
        {"__fdClose:", prim_FdClose},            {"__fdTimeout:ms:", prim_FdTimeout},
        {"__byteSize:", prim_ByteSize},          {"__percentDecode:", prim_PercentDecode},
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
