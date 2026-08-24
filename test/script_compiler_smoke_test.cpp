#include "../scripting/include/compiler.h"

#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: script-compiler-smoke-test <main.mr>\n";
        return 2;
    }

    es_script::Compiler compiler;
    compiler.initialize();
    const bool compiled = compiler.compile(argv[1]);
    compiler.destroy();

    if (!compiled) {
        std::cerr << "Engine script compilation failed. See error_log.log.\n";
        return 1;
    }

    std::cout << "Engine script compilation passed.\n";
    return 0;
}
