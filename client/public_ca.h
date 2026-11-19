#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace lansyra {
// Public profiles carry PEM certificates only. Platform crypto validates each block.
inline std::vector<std::string> public_ca_blocks(const std::string &raw) {
    const std::string begin="-----BEGIN CERTIFICATE-----", end="-----END CERTIFICATE-----";
    std::vector<std::string> blocks;
    if (raw.empty() || raw.size()>65536) throw std::runtime_error("Invalid public CA size");
    size_t position=0;
    for (;;) {
        position=raw.find_first_not_of(" \t\r\n",position);
        if (position==std::string::npos) break;
        if (blocks.size()==64 || raw.compare(position,begin.size(),begin)!=0)
            throw std::runtime_error("Public CA must contain only PEM certificates");
        auto close=raw.find(end,position+begin.size());
        if (close==std::string::npos) throw std::runtime_error("Incomplete public CA");
        auto body=raw.substr(position+begin.size(),close-position-begin.size());
        if (body.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\r\n")!=std::string::npos)
            throw std::runtime_error("Invalid public CA encoding");
        blocks.push_back(raw.substr(position,close+end.size()-position));
        position=close+end.size();
    }
    if (blocks.empty()) throw std::runtime_error("Empty public CA");
    return blocks;
}
}
