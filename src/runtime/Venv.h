#pragma once
#include <string>

namespace protoST {

// Returns 0 on success, non-zero on error (also writes a message to stderr).
int venvCreate(const std::string& venvPath,
               const std::string& homeBin,
               const std::string& version);

// Returns active venv path (from STENV or walking up from cwd) or "" if none.
std::string venvDiscover(const std::string& cwd);

// Prints info about the active venv to stdout. Returns 0 if active, 1 if none.
int  venvInfo(const std::string& cwd);

// Writes activate snippet for the current shell to stdout. POSIX only in F1.
int  venvActivateSnippet(const std::string& venvPath);

} // namespace protoST
