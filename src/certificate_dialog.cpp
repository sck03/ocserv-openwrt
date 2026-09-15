#include "certificate_dialog.h"
#include <commctrl.h>

namespace bridge {
namespace {
struct DialogContext {
    CertificateRequest* request;
    const std::function<void(HWND)>* preview_capture;
    bool captured = false;
};
HRESULT CALLBACK certificate_dialog_callback(HWND window, UINT notification, WPARAM elapsed, LPARAM, LONG_PTR data) {
    auto& context = *reinterpret_cast<DialogContext*>(data);
    if (notification == TDN_TIMER && context.request->cancelled)
        SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
    else if (notification == TDN_TIMER && elapsed >= 300 && *context.preview_capture && !context.captured) {
        context.captured = true;
        (*context.preview_capture)(window);
        SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
    }
    return S_OK;
}
}
bool confirm_server_certificate(HWND owner, Language language, const std::shared_ptr<CertificateRequest>& request,
    const std::function<void(HWND)>& preview_capture) {
    const bool changed = !request->previous_pin.empty();
    auto tr = [language](const wchar_t* zh, const wchar_t* en) { return choose(language, zh, en); };
    auto title = tr(L"确认服务器证书", L"Confirm server certificate");
    auto instruction = changed ? tr(L"服务器证书指纹已变化", L"The server's public key has changed") :
        tr(L"首次连接：请确认服务器证书", L"First connection: confirm the server certificate");
    std::wstring content = changed ?
        tr(L"此服务器提供的公钥与上次保存的不同。请向管理员核实更换原因，再确认新指纹。", L"This server presented a different public key. Confirm the change and the new fingerprint with your administrator.") :
        tr(L"无法通过证书颁发机构验证此服务器。请核对地址和指纹；确认后将记住指纹并继续连接。", L"A certificate authority could not verify this server. Check the address and fingerprint; confirming remembers this key and continues connecting.");
    content += tr(L"\n\n服务器：", L"\n\nServer: ") + server_origin(request->server);
    content += tr(L"\n\n当前指纹：\n", L"\n\nCurrent fingerprint:\n") + wide(request->pin);
    if (changed) content += tr(L"\n\n上次保存的指纹：\n", L"\n\nPreviously saved fingerprint:\n") + wide(request->previous_pin);
    auto details = request->reason + L"\n\n" + request->details;
    TASKDIALOG_BUTTON buttons[] = {
        {IDYES, changed ? tr(L"确认更换并记住", L"Confirm change and remember") : tr(L"信息准确，记住并连接", L"Accurate information")},
        {IDCANCEL, tr(L"取消", L"Cancel")}
    };
    TASKDIALOGCONFIG dialog{};
    DialogContext context{request.get(), &preview_capture};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = owner;
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_CALLBACK_TIMER | TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.pszWindowTitle = title;
    dialog.pszMainIcon = TD_WARNING_ICON;
    dialog.pszMainInstruction = instruction;
    dialog.pszContent = content.c_str();
    dialog.pszExpandedInformation = details.c_str();
    dialog.pszExpandedControlText = tr(L"收起证书详情", L"Hide certificate details");
    dialog.pszCollapsedControlText = tr(L"查看证书详情", L"Show certificate details");
    dialog.cButtons = 2; dialog.pButtons = buttons;
    dialog.nDefaultButton = IDCANCEL;
    dialog.cxWidth = 360;
    dialog.pfCallback = certificate_dialog_callback;
    dialog.lpCallbackData = reinterpret_cast<LONG_PTR>(&context);
    int chosen = IDCANCEL;
    HRESULT result = TaskDialogIndirect(&dialog, &chosen, nullptr, nullptr);
    return SUCCEEDED(result) && chosen == IDYES && !request->cancelled;
}
}
