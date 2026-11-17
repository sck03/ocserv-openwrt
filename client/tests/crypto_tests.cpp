// SPDX-License-Identifier: LGPL-2.1-or-later
// Public deterministic vectors from stoken 0.93 tests/tokencode-v{2,3,4}.
// They exercise AES, SHA and Base64 decoding through the new Nettle 4 API.
#include <stoken.h>
#include <cstring>
#include <iostream>
#include <memory>

int main() {
    struct Vector { const char *token, *password, *device, *pin; time_t when; const char *expected; };
    const Vector vectors[] = {
        {"http://127.0.0.1/securid/ctf?ctfData=258491750817210752367175001073261277346642631755724762324173166222072472716737543", nullptr, nullptr, "9999", 1409757465, "65365425"},
        {"http://127.0.0.1/securid/ctf?ctfData=AwEBWoDfCnTYFHKM8RvGCXEbSiReGdGgA88EDrIP6EhAe8tzPkIGiAaXXtInt6UHsgM1NFmwuTVjOlJXIpNXxmj7Iud0hfL2kLmIdPgRiS6jP%2FO8q9Fcpwo%2F8tLukZRoIU7gdFjpSl3teO%2FMWlr9rJBZtkTW4q0mAehJ1tl4l0vGjcDycwmIgyzeods7F43ljVETNZjlHkDTudosNSvmS%2Bl643vFrM6NGT%2BHLrlCX0igfo5i4yaUKwDDS4AiAEq%2Bpp0dv8ZzkpZIEJikRzeWaxpfml%2BmsakJ%2BYAVFcfBoR2%2BLzr1%2Flp7mX%2BwMw4TFDZ4hS88BMY3P7uV9%2BGNz08Euaru779p4XDde0JxrPGPuGjWxUBt%2BN5aUjJkcXvAtswhfirK", "Correct_horse!battery&staple", "a01c4380-fc01-4df0-b113-7fb98ec74694", "1234", 1410710132, "27957523"},
        {"com.rsa.securid://ctf?ctfData=BAABaKfqKwgEkWDGEgaxp2ZGloQ7dDw2A8PglNlhP8qCBhtop%2BorCASRYMYSBrGnZkaWhDt0PDYDw%2BCU2WE%2FyoIGGznAfd6pVLcjsDtpKoG5APTUrXL51Bdnf%2FCDvZanmNEGhzDCbsDsFTFyLgKzdht0X1tKt23tFwP%2FDYg9xDS1HvS8Jy3QfT04PFNm%2BdCUUZyMIoTzdFT01msNHtrRxePWU7cB32CE48U%2BKlbW4hPyhphJhkg5qxUA38cD05J1s44hI3FTjaq%2FAhAKAQWsDy7TZE6qtU5f6cYIzdr5PKILhTyCeXRxiYuLinAkXEHWm%2F%2FrFKyroQpn%2FVYAA3NLS59HWBQwWyS2kzhtlzJh%2BI25IMhdhLvVdXdjuNzRxkwjc74z", nullptr, "d82c-467c-56fb-2058-edf8-add6", "1234", 1650391605, "891523"},
    };
    for (const auto &v : vectors) {
        std::unique_ptr<stoken_ctx, decltype(&stoken_destroy)> context(stoken_new(), stoken_destroy);
        char code[STOKEN_MAX_TOKENCODE + 1]{};
        if (!context || stoken_import_string(context.get(), v.token) ||
            stoken_decrypt_seed(context.get(), v.password, v.device) ||
            stoken_compute_tokencode(context.get(), v.when, v.pin, code) || std::strcmp(code, v.expected)) {
            std::cerr << "Software token vector failed.\n";
            return 1;
        }
    }
    std::cout << "{\"passed\":3}\n";
}
