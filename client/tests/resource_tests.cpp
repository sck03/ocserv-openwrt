// Exercise bounded queues and real Win32 controls without a VPN or host input.
#include "event_queue.h"
#include "ui.h"
#include <psapi.h>
#include <iostream>
#include <stdexcept>

using namespace bulijie;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
SIZE_T private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                               sizeof(memory)) != FALSE,
          "memory counters unavailable");
    return memory.PrivateUsage;
}
void flood(EventQueue &queue, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        Event log;
        log.kind = Event::Kind::Log;
        log.text.assign(8192, L'x');
        queue.push(std::move(log));
        Event stats;
        stats.kind = Event::Kind::Statistics;
        stats.statistics.downloaded = i;
        queue.push(std::move(stats));
        Event state;
        state.state = i % 2 ? State::Connected : State::Reconnecting;
        queue.push(std::move(state));
    }
}
} // namespace
int main() {
    try {
        EventQueue queue;
        Event initial;
        check(queue.push(initial), "empty queue must wake UI");
        check(!queue.push(initial), "pending queue must coalesce UI wakeups");
        flood(queue, 100000);
        Event prompt;
        prompt.kind = Event::Kind::Prompt;
        prompt.prompt = std::make_shared<Prompt>();
        queue.push(prompt);
        Event terminal;
        terminal.state = State::Failed;
        terminal.terminal = true;
        queue.push(terminal);
        auto batch = queue.take();
        size_t characters = 0, logs = 0, statistics = 0;
        for (const auto &event : batch) {
            if (event.kind == Event::Kind::Log) {
                characters += event.text.size();
                ++logs;
            } else if (event.kind == Event::Kind::Statistics) {
                ++statistics;
                check(event.statistics.downloaded == 99999, "statistics are stale");
            }
        }
        check(characters <= EventQueue::MaxLogCharacters && logs <= EventQueue::MaxLogEvents,
              "log queue exceeded budget");
        check(statistics == 1 && batch.size() == logs + 4, "snapshots accumulated");
        check(batch[batch.size() - 2].prompt == prompt.prompt && batch.back().terminal,
              "log pressure lost prompt or terminal state");
        batch.clear();
        check(queue.take().empty() && queue.push(initial), "drain did not reset queue");
        queue.take();
        DWORD handles_before = 0, handles_after = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles_before);
        SIZE_T memory_before = private_bytes();
        for (unsigned cycle = 0; cycle < 1000; ++cycle) {
            flood(queue, 100);
            Event question;
            question.kind = Event::Kind::Prompt;
            question.prompt = std::make_shared<Prompt>();
            queue.push(std::move(question));
            queue.take();
        }
        GetProcessHandleCount(GetCurrentProcess(), &handles_after);
        SIZE_T memory_after = private_bytes();
        check(handles_after <= handles_before + 2, "prompt event handles leaked");
        check(memory_after <= memory_before + 8 * 1024 * 1024, "queue memory grew across drains");

        ui::LogWindow log;
        log.append(L"first entry");
        log.show(nullptr, Language::English);
        HWND window = log.handle(), text = GetDlgItem(window, ui::LogText);
        auto initial_text = read_window_text(text);
        ShowWindow(window, SW_HIDE);
        for (unsigned i = 0; i < 10000; ++i)
            log.append(L"bounded history " + std::to_wstring(i) + std::wstring(80, L'x'));
        SendMessageW(window, WM_TIMER, 1, 0);
        check(read_window_text(text) == initial_text, "hidden log control was repainted");
        log.show(nullptr, Language::English);
        auto current = read_window_text(text, 1024 * 1024);
        check(current.size() <= 512 * 1024 && current.find(L"bounded history 9999") != std::wstring::npos,
              "show did not refresh bounded history");
        log.append(L"batched update");
        check(read_window_text(text, 1024 * 1024) == current, "append repainted synchronously");
        SendMessageW(window, WM_TIMER, 1, 0);
        check(read_window_text(text, 1024 * 1024).find(L"batched update") != std::wstring::npos,
              "visible log did not refresh");
        SendMessageW(window, WM_COMMAND, ui::LogClear, 0);
        check(read_window_text(text).empty(), "clear did not reset visible history");
        std::cout << "{\"passed\":8,\"events\":600000,\"drain_cycles\":1000,\"private_bytes_before\":"
                  << memory_before << ",\"private_bytes_after\":" << memory_after
                  << ",\"handles_before\":" << handles_before << ",\"handles_after\":" << handles_after
                  << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
