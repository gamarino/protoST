#pragma once

namespace protoST {
    // Sentinel header so the include path is exercised by the build.
    // Real STRuntime declaration arrives in Task 38.
    inline const char* versionString() { return "protoST 0.1.0-pre"; }
}
