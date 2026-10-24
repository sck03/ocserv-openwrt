#include "network_policy.h"
#include <fwpmu.h>
#include <rpc.h>
#include <algorithm>

namespace vpn {
namespace {
// These SDK identifiers are omitted by some MinGW-w64 fwpmu.h versions.
// Values: Microsoft win32metadata, RecompiledIdlHeaders/um/fwpmu.h.
constexpr GUID ConnectV6{0x4a72393b,0x319f,0x44bc,{0x84,0xc3,0xba,0x54,0xdc,0xb3,0xb6,0xb4}};
constexpr GUID ReceiveV6{0xa3b42c97,0x9f04,0x4672,{0xb8,0x7e,0xce,0xe9,0xc4,0x83,0x25,0x7f}};
constexpr GUID AppId{0xd78e1e87,0x8644,0x4ea5,{0x94,0x37,0xd8,0x09,0xec,0xef,0xc9,0x71}};
constexpr GUID Flags{0x632ce23b,0x5167,0x435c,{0x86,0xd7,0xe9,0x03,0x68,0x4a,0xa8,0x0c}};
constexpr DWORD DynamicSession = 1;
}
bool ipv4_full_tunnel(const std::vector<std::string> &routes) {
    return routes.empty() || std::any_of(routes.begin(), routes.end(), [](const auto &route) {
        return route == "0.0.0.0/0" || route == "0.0.0.0/0.0.0.0";
    }) || (std::any_of(routes.begin(), routes.end(), [](const auto &route) {
        return route == "0.0.0.0/1" || route == "0.0.0.0/128.0.0.0";
    }) && std::any_of(routes.begin(), routes.end(), [](const auto &route) {
        return route == "128.0.0.0/1" || route == "128.0.0.0/128.0.0.0";
    }));
}
Ipv6Guard::~Ipv6Guard() { reset(); }
void Ipv6Guard::reset() {
    if (engine_) {
        FwpmEngineClose0(engine_);
        engine_ = nullptr;
    }
}
bool Ipv6Guard::require(bool enabled, std::wstring &error) {
    if (!enabled) { reset(); return true; }
    if (engine_)
        return true;
    FWPM_SESSION0 session{};
    session.flags = DynamicSession;
    DWORD code = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine_);
    FWP_BYTE_BLOB *application = nullptr;
    if (!code) {
        wchar_t module[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, module, 32768);
        code = length && length < 32768 ? FwpmGetAppIdFromFileName0(module, &application) : ERROR_BAD_PATHNAME;
    }
    if (!code)
        code = FwpmTransactionBegin0(engine_, 0);
    GUID key{};
    if (!code && FAILED(CoCreateGuid(&key)))
        code = ERROR_GEN_FAILURE;
    FWPM_SUBLAYER0 layer{};
    layer.subLayerKey = key;
    layer.displayData.name = const_cast<wchar_t *>(L"Lansyra IPv4 full tunnel");
    layer.weight = 0xffff;
    if (!code)
        code = FwpmSubLayerAdd0(engine_, &layer, nullptr);
    FWPM_FILTER_CONDITION0 conditions[2]{};
    conditions[0].fieldKey = AppId;
    conditions[0].matchType = FWP_MATCH_EQUAL;
    conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.byteBlob = application;
    conditions[1].fieldKey = Flags;
    conditions[1].matchType = FWP_MATCH_FLAGS_NONE_SET;
    conditions[1].conditionValue.type = FWP_UINT32;
    conditions[1].conditionValue.uint32 = FWP_CONDITION_FLAG_IS_LOOPBACK;
    for (const auto &id : {ConnectV6, ReceiveV6}) {
        if (code)
            break;
        FWPM_FILTER0 filter{};
        filter.displayData.name = layer.displayData.name;
        filter.layerKey = id;
        filter.subLayerKey = key;
        filter.weight.type = FWP_UINT8;
        filter.weight.uint8 = 15;
        // A soft permit in this sublayer exempts the VPN transport; existing
        // Windows firewall blocks in other sublayers still take precedence.
        filter.action.type = FWP_ACTION_PERMIT;
        filter.numFilterConditions = 2;
        filter.filterCondition = conditions;
        code = FwpmFilterAdd0(engine_, &filter, nullptr, nullptr);
        if (!code) {
            filter.weight.uint8 = 1;
            filter.action.type = FWP_ACTION_BLOCK;
            filter.numFilterConditions = 1;
            filter.filterCondition = &conditions[1];
            code = FwpmFilterAdd0(engine_, &filter, nullptr, nullptr);
        }
    }
    if (!code)
        code = FwpmTransactionCommit0(engine_);
    if (application)
        FwpmFreeMemory0(reinterpret_cast<void **>(&application));
    if (code) {
        reset();
        error = L"无法启用 IPv6 防绕行保护。 / Could not protect an IPv4-only full tunnel: " + system_error(code);
    }
    return code == ERROR_SUCCESS;
}
} // namespace vpn
