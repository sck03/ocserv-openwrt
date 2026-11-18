#include "ui.h"
#include <iostream>
#include <stdexcept>

class Fixture : public vpn::ui::Window {
public:
    void start() {
        if (!create(nullptr, L"Lansyra.DpiFixture", L"DPI fixture", 400, 240, WS_OVERLAPPEDWINDOW))
            throw std::runtime_error("window creation failed");
        button(L"Fixture", 1, 20, 20, 100, 30);
    }
};
int main() {
    if (vpn::ui::profile_file_name(L"192.168.1.1:4443") != L"Lansyra-192.168.1.1_4443.vpn" ||
        vpn::ui::profile_file_name(L"\\\\server\\share").find(L'\\') != std::wstring::npos ||
        vpn::ui::profile_file_name(L"CON") != L"Lansyra-CON.vpn") throw std::runtime_error("unsafe export suggestion");
    Fixture window;
    window.start();
    RECT original{}, control{};
    GetWindowRect(window.handle(), &original);
    GetWindowRect(window.item(1), &control);
    const auto height = control.bottom - control.top;
    const int initialDpi = window.px(96), largerDpi = initialDpi * 3 / 2;
    const DWORD handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; ++i) {
        RECT larger{original.left, original.top, original.left + (original.right - original.left) * 3 / 2,
                    original.top + (original.bottom - original.top) * 3 / 2};
        SendMessageW(window.handle(), WM_DPICHANGED, MAKELONG(largerDpi, largerDpi), reinterpret_cast<LPARAM>(&larger));
        GetWindowRect(window.item(1), &control);
        if (control.bottom - control.top <= height) throw std::runtime_error("controls were not scaled");
        SendMessageW(window.handle(), WM_DPICHANGED, MAKELONG(initialDpi, initialDpi), reinterpret_cast<LPARAM>(&original));
    }
    if (GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) > handles + 2) throw std::runtime_error("DPI font handles leaked");
    std::cout << "20 DPI transitions kept controls scaled and font handles bounded\n";
}
