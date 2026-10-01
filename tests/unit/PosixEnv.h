#pragma once
// setenv/unsetenv for the unit tests on Windows, whose C runtime has
// _putenv_s instead. Elsewhere this header adds nothing.
#if defined(_WIN32)
#include <cstdlib>
inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name, value);
}
// An empty value removes the variable.
inline int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif
