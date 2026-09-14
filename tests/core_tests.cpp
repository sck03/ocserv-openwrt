#include "common.h"
#include "session.h"
#include "settings.h"
#include <iostream>
#include <cstdio>
#include <memory>

using namespace bridge;
static int failures = 0, checks = 0;
static void check(bool value, const char* name) { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << name << "\n"; } }
int main(int argc, char** argv) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2), &data) || openconnect_init_ssl()) return 2;
    if (argc == 4 && std::string(argv[1]) == "--import-profile") {
        Profile current; current.server=L"https://192.168.19.254:4443";
        std::wstring error;
        bool ok=import_connection(wide(argv[2]),current,error,wide(argv[3]));
        std::cout << (ok ? "imported\n" : "rejected\n");
        WSACleanup();
        return ok ? 0 : 1;
    }
    if (argc == 5 && (std::string(argv[1]) == "--authenticate" || std::string(argv[1]) == "--cancel-auth")) {
        ConnectOptions options;
        options.authentication_only = true;
        options.profile.server = wide(argv[2]);
        if (std::string(argv[3]).rfind("ca:",0) == 0) options.profile.ca_file = wide(std::string(argv[3]).substr(3));
        else if (std::string(argv[3]) != "-") options.profile.pin = argv[3];
        options.username = argv[4];
        std::getline(std::cin, options.password);
        Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Event result;
        {
            Session session(std::move(options), [&](Event event) { if (event.terminal) { result = std::move(event); SetEvent(done.get()); } });
            if (!session.start()) return 3;
            if (std::string(argv[1]) == "--cancel-auth") { Sleep(150); session.cancel(); }
            if (WaitForSingleObject(done.get(), 20000) != WAIT_OBJECT_0) { session.cancel(); std::cerr << "authentication timeout\n"; return 4; }
        }
        std::cout << "state=" << static_cast<int>(result.state) << " error=" << static_cast<int>(result.error) << "\n";
        WSACleanup();
        return result.error == Error::None ? 0 : 10 + static_cast<int>(result.error);
    }
    std::wstring value;
    check(normalize_server(L"192.168.19.254:4443", value) && value == L"https://192.168.19.254:4443", "ocserv endpoint normalization");
    check(normalize_server(L"  HTTPS://vpn.example.org/company  ", value) && value == L"https://vpn.example.org/company", "trim and HTTPS case normalization");
    check(normalize_server(L"https://[2001:db8::1]:4443/vpn", value), "IPv6 URL");
    for (const wchar_t* invalid : {L"", L"http://vpn.example.org", L"https://user:secret@host", L"https://host:0", L"https://host:65536",
        L"https://host:abc", L"https://host:", L"https://host\\path", L"https://[not-ip]", L"https://host\nInjected: 1", L"https://host/?x=y", L"https://host/#fragment", L"https:///x"})
        check(!normalize_server(invalid, value), "reject unsafe or malformed endpoint");
    Prefix prefix{};
    check(parse_prefix("10.77.12.34/255.255.255.0", prefix) && prefix.length == 24 && ntohl(prefix.address.Ipv4.sin_addr.s_addr) == 0x0a4d0c00, "normalize dotted IPv4 route mask");
    check(parse_prefix("192.0.2.9/32", prefix) && prefix.length == 32, "IPv4 host route");
    check(parse_prefix("0.0.0.0/0", prefix) && prefix.length == 0, "default route");
    check(parse_prefix("2001:db8:1::99/64", prefix) && prefix.length == 64 && prefix.address.Ipv6.sin6_addr.u.Byte[15] == 0, "normalize IPv6 route");
    check(parse_prefix("2001:db8::1/128", prefix) && prefix.length == 128, "IPv6 host route");
    for (const char* invalid : {"10.0.0.0/255.0.255.0", "10.0.0.0/33", "::/129", "1.2.3.4/-1", "1.2.3.4/", "1.2.3.4/24 trailing", "127.0.0.1 & calc", "1.2.3.999/24", "fe80::1%3/64", "host.example/32"})
        check(!parse_prefix(invalid, prefix), "reject malformed or injectable route");
    check(valid_pin("pin-sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="), "complete SHA-256 pin");
    check(!valid_pin("pin-sha256:AAAA"), "reject abbreviated pin");
    check(!valid_pin("sha1:0000000000000000000000000000000000000000"), "reject SHA-1 pin");
    check(!valid_pin("pin-sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA AAAA="), "reject whitespace in pin");
    check(valid_pin(""), "system trust mode");
    std::wstring unicode = L"布利杰VPN / 中英文";
    check(wide(utf8(unicode)) == unicode, "Unicode survives UTF-8 conversion");
    check(connection_duration(65) == L"00:01:05", "connected duration uses minutes and seconds");
    check(connection_duration(90061) == L"25:01:01", "connected duration does not wrap after one day");
    auto log = redacted("authentication for employee failed with test-password", "employee", "test-password");
    check(log.find("employee") == std::string::npos && log.find("test-password") == std::string::npos, "credentials removed from diagnostics");
    check(redacted("Set-Cookie: webvpn=private-token", "", "").find("private-token") == std::string::npos, "session cookie omitted from diagnostics");
    check(redacted(std::string(4000, 'x'), "", "").size() == 2000, "diagnostic length bound");
    std::string secret = "sensitive";
    erase(secret); check(secret.empty(), "credential buffer cleared");
    Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    Event completion;
    {
        ConnectOptions options;
        options.authentication_only = true;
        options.profile.server = L"https://127.0.0.1:9";
        options.username = "fixture"; options.password = "test-password";
        Session session(std::move(options), [&](Event event){ if(event.terminal) {completion = std::move(event); SetEvent(done.get());} });
        session.cancel();
        check(session.start(), "start cancellable worker");
        check(WaitForSingleObject(done.get(), 5000) == WAIT_OBJECT_0, "cancellation completes without network I/O");
    }
    check(completion.terminal && completion.state == State::Idle && completion.error == Error::None, "cancelled startup restores idle state");
    {
        Profile test_profile;
        test_profile.server=L"https://credential-test-" + std::to_wstring(GetCurrentProcessId()) + L".invalid";
        DWORD error=0;
        const std::wstring username=L"synthetic-test-user", password=L"synthetic-password-测试";
        std::wstring user, pass;
        check(save_credentials(test_profile,username,password,error),"save synthetic credential in the Windows credential manager");
        check(read_credentials(test_profile,user,pass) && user==username && pass==password,"read protected Unicode credentials");
        erase(pass);
        Profile changed=test_profile; changed.pin="pin-sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        check(!read_credentials(changed,user,pass),"trust changes cannot retrieve old credentials");
        changed=test_profile; changed.server += L"/other";
        check(!read_credentials(changed,user,pass),"another server URL cannot retrieve old credentials");
        check(forget_credentials(test_profile,error),"remove synthetic credential after verification");
        check(!read_credentials(test_profile,user,pass),"forgotten password is no longer available");
    }
    std::cout << checks << " checks, " << failures << " failures; OpenConnect " << openconnect_get_version() << "\n";
    WSACleanup();
    return failures ? 1 : 0;
}
