#pragma once
// The C++ exception protoST code throws for an error that becomes a Smalltalk
// Error, and the one way to read the text of a native exception.
//
// A protoST String may contain U+0000, and an error text often embeds user
// data (a selector, a key, a path, the digits of a number, a fragment of the
// input). std::exception::what() is a C string: a reader that takes it ends the
// text at the first NUL, so a message about the string 'a', NUL, 'b' would
// read as 'a'. NativeError keeps the full std::string next to the copy
// std::runtime_error holds, and nativeErrorText answers it.
//
// Every native error protoST raises is a NativeError (ClassedErrorSignal, an
// error of a named kernel class, derives from it). Every site that turns a C++
// exception into a Smalltalk error or a printed diagnostic reads its text
// through nativeErrorText, never through what(): the native-exception bridge,
// an actor message's rejection, the uncaught-error report, the debugger's and
// the DAP server's evaluation errors; it is printed with writeWholeLine, never
// with "%s". Exceptions of other types (from the C++
// library, from protoIO) answer what(), which is all they have.

#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace protoST {

class NativeError : public std::runtime_error {
public:
    explicit NativeError(const std::string& message)
        : NativeError(std::make_shared<const std::string>(message)) {}
    explicit NativeError(const char* message)
        : NativeError(std::make_shared<const std::string>(message)) {}

    // The full message, NUL bytes included.
    const std::string& message() const noexcept { return *message_; }

private:
    // Shared, so that copying the exception (std::make_exception_ptr, a catch
    // by value) never allocates and never throws, as for the standard
    // exception types.
    explicit NativeError(std::shared_ptr<const std::string> message)
        : std::runtime_error(*message), message_(std::move(message)) {}

    std::shared_ptr<const std::string> message_;
};
static_assert(std::is_nothrow_copy_constructible_v<NativeError>,
              "copying a NativeError must not throw");

// The text of `e`: the full message of a NativeError, what() for any other
// exception.
inline std::string nativeErrorText(const std::exception& e) {
    if (const auto* n = dynamic_cast<const NativeError*>(&e)) return n->message();
    return e.what();
}

// Writes `text` and a line break to `out`, every byte of it: a diagnostic or a
// printed value written with "%s" or puts would end at a NUL in the text.
inline void writeWholeLine(std::FILE* out, std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), out);
    std::fputc('\n', out);
}

} // namespace protoST
