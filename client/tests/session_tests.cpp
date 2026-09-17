// Test-only driver for the real OpenConnect callbacks. Requests are restricted to loopback.
#include "session.h"
#include "vendor/json.hpp"
#include <chrono>
#include <iostream>
#include <mutex>

using namespace bulijie;
using Json = nlohmann::json;
namespace {
const char *state_name(State state) {
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Connecting:
        return "connecting";
    case State::Authenticating:
        return "authenticating";
    case State::Configuring:
        return "configuring";
    case State::Connected:
        return "connected";
    case State::Reconnecting:
        return "reconnecting";
    case State::Disconnecting:
        return "disconnecting";
    default:
        return "failed";
    }
}
const char *prompt_name(Prompt::Kind kind) {
    switch (kind) {
    case Prompt::Kind::Text:
        return "text";
    case Prompt::Kind::Password:
        return "password";
    case Prompt::Kind::Selection:
        return "selection";
    case Prompt::Kind::Certificate:
        return "certificate";
    default:
        return "notice";
    }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::cerr << "Usage: native_session_tests <synthetic-loopback-fixture.json>\n";
        return 2;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0 || openconnect_init_ssl() != 0)
        return 2;
    try {
        std::wstring error;
        std::string raw;
        if (!read_file(std::filesystem::path(argv[1]), raw, error, 65536))
            throw std::runtime_error("Cannot read fixture");
        Json input = Json::parse(raw);
        erase(raw);
        Profile profile;
        profile.name = L"Synthetic loopback test";
        profile.gateway = wide(input.at("gateway").get<std::string>());
        std::wstring normalized, origin;
        if (!normalize_gateway(profile.gateway, normalized, &origin) ||
            !(origin.rfind(L"https://127.0.0.1:", 0) == 0 || origin.rfind(L"https://localhost:", 0) == 0 ||
              origin.rfind(L"https://[::1]:", 0) == 0))
            throw std::runtime_error("Only loopback fixture gateways are allowed");
        auto directory = std::filesystem::u8path(input.at("directory").get<std::string>());
        ProfileStore store(directory);
        std::vector<Profile> profiles;
        Preferences preferences;
        if (!store.load(profiles, preferences, error))
            throw std::runtime_error("Cannot load test profiles");
        if (input.value("reuse_profile", false) && !profiles.empty()) {
            auto gateway = profile.gateway;
            profile = profiles.front();
            profile.gateway = gateway;
            if (!store.save(profile, error))
                throw std::runtime_error("Cannot update test gateway");
        } else {
            profile.username = input.value("username", std::string("fixture-user"));
            profile.password = input.value("saved_password", std::string());
            profile.batch_mode = input.value("batch_mode", false);
            profile.group = input.value("group", std::string());
            profile.server_pin = input.value("pin", std::string());
            profile.fixed_pin = input.value("fixed_pin", !profile.server_pin.empty());
            profile.ca_file = wide(input.value("ca_file", std::string()));
            profile.token_type = input.value("token_type", -1);
            profile.token = input.value("token", std::string());
            profile.certificate_file = wide(input.value("certificate_file", std::string()));
            profile.key_file = wide(input.value("key_file", std::string()));
            profile.disable_udp = input.value("disable_udp", false);
            profile.reconnect_timeout = 5;
            if (!store.save(profile, error))
                throw std::runtime_error("Cannot save test profile");
        }
        bool tunnel = input.value("tunnel", false);
        std::mutex lock;
        Json output = {{"states", Json::array()}, {"prompts", Json::array()}, {"logs", Json::array()},
                       {"terminal", "none"},      {"canceled", false},        {"protocols", Json::array()}};
        for (const auto &protocol : supported_protocols())
            output["protocols"].push_back(protocol.name);
        output["stoken"] = openconnect_has_stoken_support() != 0;
        output["oath"] = openconnect_has_oath_support() != 0;
        output["system_keys"] = openconnect_has_system_key_support() != 0;
        unsigned prompts = 0;
        bool connected = false;
        auto started = std::chrono::steady_clock::now();
        Session session(
            profile, &store, Language::English,
            [&](Event event) {
                std::lock_guard<std::mutex> guard(lock);
                if (event.kind == Event::Kind::State) {
                    output["states"].push_back(state_name(event.state));
                    if (event.state == State::Connected)
                        connected = true;
                    if (event.terminal) {
                        output["terminal"] = state_name(event.state);
                        output["error"] = utf8(event.text);
                    }
                } else if (event.kind == Event::Kind::Log)
                    output["logs"].push_back(utf8(event.text));
                else if (event.kind == Event::Kind::Statistics)
                    output["statistics"] = {{"ipv4", utf8(event.statistics.ipv4)},
                                            {"ipv6", utf8(event.statistics.ipv6)},
                                            {"downloaded", event.statistics.downloaded},
                                            {"uploaded", event.statistics.uploaded}};
                else if (event.kind == Event::Kind::Prompt) {
                    auto p = event.prompt;
                    ++prompts;
                    output["prompts"].push_back({{"kind", prompt_name(p->kind)},
                                                 {"field", p->field},
                                                 {"server", utf8(p->server)},
                                                 {"initial_empty", p->initial.empty()},
                                                 {"changed_pin", !p->previous_pin.empty()}});
                    bool accept = prompts <= input.value("max_prompts", 16u);
                    if (p->kind == Prompt::Kind::Certificate)
                        accept = accept && input.value("accept_certificate", false);
                    if (input.value("cancel_prompt", std::string()) == prompt_name(p->kind))
                        accept = false;
                    if (!input.value("allow_redirect_prompts", false) && p->server != origin)
                        accept = false;
                    std::string response;
                    if (p->kind == Prompt::Kind::Text)
                        response = input.value("username", std::string("fixture-user"));
                    if (p->kind == Prompt::Kind::Password)
                        response = input.value("password", std::string("fixture-password"));
                    if (input.contains("responses") && input["responses"].contains(p->field))
                        response = input["responses"][p->field].get<std::string>();
                    if (p->kind == Prompt::Kind::Selection && !p->choices.empty()) {
                        size_t index = input.value("choice", static_cast<size_t>(0));
                        response = p->choices[std::min(index, p->choices.size() - 1)].value;
                    }
                    if (!accept)
                        output["canceled"] = true;
                    p->answer(accept, std::move(response));
                }
            },
            !tunnel);
        session.set_log_level(3);
        if (!session.start())
            throw std::runtime_error("Cannot start session");
        bool timeout = false, cancel_sent = false;
        while (!session.finished()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
            bool is_connected;
            {
                std::lock_guard<std::mutex> guard(lock);
                is_connected = connected;
            }
            if (tunnel && is_connected && elapsed > input.value("tunnel_duration_ms", 1800))
                session.cancel();
            if (input.contains("cancel_after_ms") && !cancel_sent &&
                elapsed > input["cancel_after_ms"].get<int>()) {
                cancel_sent = true;
                session.cancel();
                std::lock_guard<std::mutex> guard(lock);
                output["canceled"] = true;
            }
            if (elapsed > 60000) {
                timeout = true;
                session.cancel();
            }
            session.request_statistics();
            Sleep(30);
        }
        output["timeout"] = timeout;
        output["elapsed_ms"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                .count();
        if (store.load(profiles, preferences, error) && !profiles.empty()) {
            output["saved_password_matches_response"] =
                profiles.front().password == input.value("password", std::string("fixture-password"));
            output["saved_username"] = profiles.front().username;
            output["saved_pin"] = !profiles.front().server_pin.empty();
        }
        std::cout << output.dump() << "\n";
        return timeout ? 1 : 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << "\n";
        return 2;
    }
}
