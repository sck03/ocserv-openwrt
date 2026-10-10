#include "event_queue.h"
#include "vendor/json.hpp"
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <shellapi.h>

using namespace vpn;
namespace {
unsigned passed = 0;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
    ++passed;
}
Event snapshot(uint64_t session, uint64_t generation, State state) {
    Event result;
    result.session_id = session;
    result.generation = generation;
    result.state = state;
    return result;
}
void lifecycle() {
    EventCursor cursor;
    cursor.begin(12);
    check(!cursor.accept(snapshot(11, 99, State::Connected)), "old session updated new connection");
    check(cursor.accept(snapshot(12, 2, State::Connecting)), "current state rejected");
    check(!cursor.accept(snapshot(12, 1, State::Failed)), "old generation updated current state");
    cursor.cancel();
    check(!cursor.accept(snapshot(12, 3, State::Connected)), "delayed connection revived cancellation");
    Event prompt = snapshot(12, 4, State::Authenticating);
    prompt.kind = Event::Kind::Prompt;
    prompt.prompt = std::make_shared<Prompt>();
    check(!cursor.accept(prompt), "canceled session displayed an authentication prompt");
    prompt.prompt->answer(false);
    prompt.prompt->answer(true, "late-secret");
    check(prompt.prompt->done && !prompt.prompt->accepted && prompt.prompt->response.empty(),
          "late answer replaced cancellation");
    auto terminal = snapshot(12, 5, State::Idle);
    terminal.terminal = true;
    check(cursor.accept(terminal) && !cursor.accept(terminal), "terminal event was consumed twice");
    check(!cursor.accept(snapshot(12, 6, State::Connected)), "finished session became connected again");
    Event log = snapshot(12, 1, State::Connecting);
    log.kind = Event::Kind::Log;
    check(cursor.accept(log), "state coalescing discarded diagnostic logs");
    cursor.begin(13);
    check(!cursor.canceled() && !cursor.accept(prompt) && cursor.accept(snapshot(13, 1, State::Connecting)),
          "new connection inherited cancellation or an old prompt");

    EventQueue queue;
    queue.push(snapshot(12, 1, State::Connecting));
    queue.push(snapshot(13, 1, State::Connecting));
    queue.push(snapshot(12, 2, State::Connected));
    auto events = queue.take();
    check(events.size() == 2 && events[0].session_id == 13 && events[1].session_id == 12 &&
              events[1].generation == 2,
          "coalescing crossed session ownership");
    log.text.assign(8192, L'x');
    for (unsigned i = 0; i < 100; ++i)
        queue.push(log);
    queue.push(terminal);
    events = queue.take();
    check(queue.dropped() == 68 && events.back().terminal, "log pressure hid the final result or loss count");
}
void errors_and_retries() {
    for (auto error : {ErrorCategory::Network, ErrorCategory::Timeout}) {
        check(retry_delay(error, 1) == 2 && retry_delay(error, 2) == 4 && retry_delay(error, 3) == 8 &&
                  !retry_delay(error, 4) && !retry_delay(error, 0),
              "retry backoff is not bounded");
    }
    for (auto error :
         {ErrorCategory::None, ErrorCategory::Canceled, ErrorCategory::Authentication,
          ErrorCategory::Certificate, ErrorCategory::Configuration, ErrorCategory::Permission,
          ErrorCategory::Adapter, ErrorCategory::Busy, ErrorCategory::Server, ErrorCategory::Internal})
        check(!retry_delay(error, 1) &&
                  (!error_text(error, Language::English).empty()) == (error != ErrorCategory::None),
              "non-transport failure was retried or lacked an explanation");
    check(connection_error(-ETIMEDOUT, ErrorCategory::Network) == ErrorCategory::Timeout &&
              connection_error(-EPERM, ErrorCategory::Network) == ErrorCategory::Authentication &&
              connection_error(-ENOMEM, ErrorCategory::Network) == ErrorCategory::Internal &&
              connection_error(-EIO, ErrorCategory::Network) == ErrorCategory::Network,
          "core error classification changed");
}
void startup_registration() {
    // RegOverridePredefKey is process-local. The real user's Run key is never touched.
    struct Registry {
        std::wstring path = L"Software\\LinkoraVPN.Test." + wide(random_id());
        HKEY key = nullptr;
        ~Registry() {
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            if (key)
                RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    } registry;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, registry.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                          &registry.key, nullptr) == ERROR_SUCCESS &&
              RegOverridePredefKey(HKEY_CURRENT_USER, registry.key) == ERROR_SUCCESS,
          "could not isolate startup registration");
    const std::filesystem::path directory = L"C:\\Synthetic VPN\\中文 data\\";
    std::wstring error;
    check(set_login_startup(directory, true, error), "startup registration failed");
    auto read = [&](std::wstring &command) {
        wchar_t buffer[512]{};
        DWORD size = sizeof(buffer), type = 0;
        auto result = RegGetValueW(registry.key, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                   L"LinkoraVPN", RRF_RT_REG_SZ, &type, buffer, &size);
        command = buffer;
        return result;
    };
    std::wstring command;
    check(read(command) == ERROR_SUCCESS, "startup command was not stored");
    int count = 0;
    wchar_t **args = CommandLineToArgvW(command.c_str(), &count);
    bool correct = args && count == 4 && std::wstring(args[1]) == L"--data-dir" &&
                   std::filesystem::path(args[2]) == directory && std::wstring(args[3]) == L"--startup";
    if (args)
        LocalFree(args);
    check(correct, "startup arguments lost Unicode, spaces or trailing slashes");
    check(!set_login_startup(std::filesystem::path(L"C:\\" + std::wstring(300, L'x')), true, error),
          "overlong Run command was accepted");
    std::wstring retained;
    check(read(retained) == ERROR_SUCCESS && retained == command,
          "invalid startup settings replaced the working entry");
    check(set_login_startup(directory, false, error) && read(command) == ERROR_FILE_NOT_FOUND &&
              set_login_startup(directory, false, error),
          "disabling startup left an entry behind");
}
void redaction() {
    Redactor filter;
    for (const char *message :
         {"Set-Cookie : opaque-cookie", "pRoXy-AuThOrIzAtIoN\t: Basic opaque", "webvpn=opaque",
          "{\"secondary_password\" : \"opaque\"}", "{'refresh-token' : 'opaque'}", "Password = opaque",
          "<credential>opaque</credential>", "<input type=hidden name=csrf value=opaque>",
          "SAMLResponse=opaque", "authcookie=opaque", "X-Session-Token: opaque", "pa%73sword%3Dopaque",
          "{\"\\u0070assword\":\"opaque\"}", "Cookie%253Aopaque", "-----BEGIN PRIVATE KEY-----\nopaque"})
        check(filter.clean(message).find("opaque") == std::string::npos, "authentication structure leaked");
    check(filter.clean("TLS handshake failed (network unreachable)") ==
              "TLS handshake failed (network unreachable)",
          "safe troubleshooting information was lost");
    for (const char *url : {"GET https://vpn.test/group?opaque=secret#fragment",
                            "POST https://[2001:db8::1]:443/group/secret?opaque=value",
                            "Proxy https://user:secret@vpn.test/path"}) {
        auto cleaned = filter.clean(url);
        check(cleaned.find("secret") == std::string::npos && cleaned.find("opaque") == std::string::npos &&
                  cleaned.find("fragment") == std::string::npos,
              "URL authentication data leaked");
    }
    const std::string secret = "S3cret<&\" /+";
    filter.remember(secret);
    for (const auto &variant :
         {secret, std::string("S3cret%3C%26%22%20%2F%2B"), std::string("S3cret%3c%26%22+%2f%2b"),
          std::string("S3cret&lt;&amp;&quot; /+"), base64(secret), std::string("S3cret<&\\\" /+")}) {
        check(filter.clean("echo " + variant).find(variant) == std::string::npos, "encoded secret leaked");
    }
    std::string large(8190, 'x');
    large += secret + std::string(8192, 'y');
    check(filter.clean(large).find(secret) == std::string::npos, "chunk-boundary secret leaked");
    Redactor bounded;
    for (unsigned i = 0; i < 80; ++i)
        bounded.remember(std::string(4096, 'x') + std::to_string(i));
    check(bounded.clean("unremembered opaque response").find("opaque") == std::string::npos,
          "redaction budget overflow failed open");
    SessionStatus status;
    status.session_id = 42;
    status.generation = 7;
    status.state = State::Failed;
    status.error = ErrorCategory::Certificate;
    status.terminal = true;
    Statistics stats;
    stats.ipv4 = L"private-endpoint";
    stats.downloaded = 12345;
    auto report = diagnostic_report(status, stats, L"Cookie: must-not-export", 11);
    auto json = nlohmann::json::parse(report);
    check(json["session"]["id"] == 42 && json["session"]["generation"] == 7 &&
              json["session"]["error_category"] == "certificate" && json["dropped_log_entries"] == 11 &&
              json["traffic"]["downloaded_bytes"] == 12345 &&
              report.find("must-not-export") == std::string::npos &&
              report.find("private-endpoint") == std::string::npos,
          "diagnostic export lost context or exposed raw data");
}
} // namespace
int main() {
    try {
        lifecycle();
        errors_and_retries();
        redaction();
        startup_registration();
        std::cout << "{\"passed\":" << passed << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
