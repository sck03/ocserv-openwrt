#pragma once
#include <map>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace lansyra {
// Public interchange contains no credentials, local paths or executable options.
struct ConnectionFile {
    static constexpr size_t MaxBytes = 128 * 1024;
    std::string name, server, protocol = "anyconnect", trust = "prompt", pin, ca;
    void validate() const {
        auto text = [](const std::string &s, size_t limit) {
            return s.size() <= limit && s.find_first_of("\r\n\0", 0, 3) == std::string::npos;
        };
        if (!text(name, 512) || !text(server, 4096) || server.empty() || !text(protocol, 32) || protocol.empty() ||
            (trust != "pin" && trust != "ca" && trust != "prompt") || !text(pin, 55) || !text(ca, 90000))
            throw std::runtime_error("Invalid public connection profile");
        size_t nameUnits = 0;
        for (unsigned char c : name) if ((c & 0xc0) != 0x80) nameUnits += c >= 0xf0 ? 2 : 1;
        if (nameUnits > 128) throw std::runtime_error("Profile name is too long");
        if (!std::all_of(protocol.begin(), protocol.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }))
            throw std::runtime_error("Invalid VPN protocol");
        if ((trust == "pin") != !pin.empty() || (trust == "ca") != !ca.empty())
            throw std::runtime_error("Ambiguous certificate policy");
        if (!pin.empty() && (pin.size() != 55 || pin.compare(0, 11, "pin-sha256:") || pin.back() != '=' ||
            pin.substr(11, 43).find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") != std::string::npos))
            throw std::runtime_error("Invalid server pin");
        if (!pin.empty() && std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/").find(pin[53]) % 4)
            throw std::runtime_error("Noncanonical server pin");
    }
    static ConnectionFile parse(const std::string &raw) {
        if (raw.size() > MaxBytes || raw.find('\0') != std::string::npos) throw std::runtime_error("Invalid profile size");
        std::map<std::string, std::string> fields;
        bool section = false;
        std::istringstream input(raw);
        for (std::string line; std::getline(input, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            if (line == "[VPN]" && !section) { section = true; continue; }
            const auto split = line.find('=');
            if (!section || split == std::string::npos || !fields.emplace(line.substr(0, split), line.substr(split + 1)).second)
                throw std::runtime_error("Invalid or duplicate profile field");
        }
        for (const auto &field : fields)
            if (field.first != "Version" && field.first != "Name" && field.first != "Server" && field.first != "Protocol" &&
                field.first != "Trust" && field.first != "ServerPin" && field.first != "CABase64")
                throw std::runtime_error("Unsupported profile field");
        if (fields["Version"] != "1" || !fields.count("Trust")) throw std::runtime_error("Unsupported profile version");
        ConnectionFile result;
        result.name = fields["Name"]; result.server = fields["Server"];
        if (fields.count("Protocol")) result.protocol = fields["Protocol"];
        result.trust = fields["Trust"]; result.pin = fields["ServerPin"]; result.ca = fields["CABase64"];
        result.validate();
        return result;
    }
    std::string encode() const {
        validate();
        return "[VPN]\nVersion=1\nName=" + name + "\nServer=" + server + "\nProtocol=" + protocol + "\nTrust=" + trust + "\n" +
            (pin.empty() ? "" : "ServerPin=" + pin + "\n") + (ca.empty() ? "" : "CABase64=" + ca + "\n");
    }
};
}
