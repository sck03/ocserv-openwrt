// Test-only peer for the real asynchronous authentication protocol reader.
#include <iostream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
namespace {
void reply(bool report_pid = false) {
    std::cout << "cookie\nsynthetic-cookie\ngateway\n127.0.0.1:443\ngwcert\npin-sha256:"
                 "synthetic\npassword\nnever-forward\n";
    if (report_pid)
        std::cout << "resolve\n" << getpid() << '\n';
    std::cout << "\n\n" << std::flush;
}
}
int main(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--uuid" && std::string(argv[i + 1]) == "00000000-0000-4000-8000-000000000001") {
            sleep(10); // Simulate a helper that stalls before reading its input.
            return 0;
        }
        if (std::string(argv[i]) == "--uuid" && std::string(argv[i + 1]) == "00000000-0000-4000-8000-000000000002") {
            // The caller supplies exactly one pipe's capacity of valid input.
            // Return credentials without consuming it, then ignore QUIT.
            if (fcntl(STDIN_FILENO, F_SETPIPE_SZ, 4096) != 4096)
                return 2;
            reply(true);
            sleep(10);
            return 0;
        }
    }
    std::string line, mode;
    while (std::getline(std::cin, line) && line != "DONE") {
        if (line == "DATA_VAL=hang")
            mode = "hang";
        if (line == "DATA_VAL=oversized")
            mode = "oversized";
        if (line == "DATA_VAL=linger" || line == "DATA_VAL=early-exit")
            mode = line.substr(9);
    }
    std::cerr << "password=must-not-appear-in-parent-output\n";
    if (mode == "oversized")
        std::cout << std::string(20000, 'x') << std::flush;
    else if (mode != "hang")
        reply(mode == "linger" || mode == "early-exit");
    if (mode == "early-exit")
        return 0;
    while (std::getline(std::cin, line) && line != "QUIT") {
    }
    if (mode == "linger")
        sleep(10); // Reading QUIT does not guarantee that the helper actually exits.
    return 0;
}
