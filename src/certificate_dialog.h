#pragma once
#include "session.h"

namespace bridge {
bool confirm_server_certificate(HWND owner, Language language, const std::shared_ptr<CertificateRequest>& request,
    const std::function<void(HWND)>& preview_capture = {});
}
