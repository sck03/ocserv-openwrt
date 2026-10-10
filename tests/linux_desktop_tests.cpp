#include "protocol.h"
#include <iostream>
#include <stdexcept>
using namespace linkora;
int main() {
    try {
        auto check = [](bool ok) {
            if (!ok)
                throw std::runtime_error("desktop protocol regression");
        };
        for (const char *address :
             {"vpn.example.com:4443", "192.168.19.253:4443", "https://[2001:db8::1]:4443/path"})
            check(!gateway(address).empty());
        for (const char *address : {"http://vpn.test", "https://user:pass@vpn.test", "127.1", "192.168.01.1",
                                    "https://vpn.test:0", "vpn.test\nDATA_KEY=cookie"})
            check(gateway(address).empty());
        Generation generation;
        auto old = generation.begin();
        generation.cancel();
        check(!generation.accepts(old));
        auto current = generation.begin();
        check(generation.accepts(current) && !generation.accepts(old));
        AuthReply reply;
        const std::string output = "password\nnever-retain\ncookie\nopaque-cookie\ngateway\nvpn.test:"
                                   "443\ngwcert\npin-sha256:opaque\n\n\n";
        for (size_t i = 0; i < output.size() && !reply.complete; ++i)
            check(reply.append(output.data() + i, 1));
        check(reply.valid() && reply.values.size() == 3 && !reply.values.count("password"));
        AuthReply oversized;
        check(!oversized.append(std::string(8193, 'x').data(), 8193));
        AuthReply truncated;
        check(truncated.append("cookie\n", 7) && !truncated.valid());
        check(retry_delay("network", 1) == 2 && retry_delay("timeout", 3) == 8 && !retry_delay("network", 4));
        for (const char *error : {"authentication", "certificate", "adapter", "canceled", "configuration"})
            check(!retry_delay(error, 1));
        std::cout << "Linux desktop protocol, cancellation, redaction boundaries and retry checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
