// Test-only peer for the real asynchronous authentication protocol reader.
#include <iostream>
#include <string>
#include <unistd.h>
int main(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--uuid" && std::string(argv[i + 1]) == "00000000-0000-4000-8000-000000000001") {
            sleep(10); // Simulate a helper that stalls before reading its input.
            return 0;
        }
    }
    std::string line, mode;
    while (std::getline(std::cin, line) && line != "DONE") {
        if (line == "DATA_VAL=hang")
            mode = "hang";
        if (line == "DATA_VAL=oversized")
            mode = "oversized";
    }
    std::cerr << "password=must-not-appear-in-parent-output\n";
    if (mode == "oversized")
        std::cout << std::string(20000, 'x') << std::flush;
    else if (mode != "hang")
        std::cout << "cookie\nsynthetic-cookie\ngateway\n127.0.0.1:443\ngwcert\npin-sha256:"
                     "synthetic\npassword\nnever-forward\n\n\n"
                  << std::flush;
    while (std::getline(std::cin, line) && line != "QUIT") {
    }
    return 0;
}
