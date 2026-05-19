#include "protoST/STRuntime.h"
#include <iostream>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << protoST::versionString() << "\n";
    return 0;
}
