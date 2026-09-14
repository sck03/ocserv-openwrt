#include "network.h"
#include "wfp_constants.h"
#include <fwpmu.h>
#include <rpc.h>
#include <algorithm>
#include <cstring>

namespace bridge {
static bool fail(std::wstring& detail, const wchar_t* operation, DWORD code) {
    detail = std::wstring(operation) + L": " + system_error(code) + L" (" + std::to_wstring(code) + L")";
    return false;
}
static bool cancelled(HANDLE event) { return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0; }
static bool same_address(const SOCKADDR_INET& a, const SOCKADDR_INET& b) {
    if (a.si_family != b.si_family) return false;
    return a.si_family == AF_INET ? a.Ipv4.sin_addr.s_addr == b.Ipv4.sin_addr.s_addr
        : std::memcmp(&a.Ipv6.sin6_addr, &b.Ipv6.sin6_addr, sizeof(IN6_ADDR)) == 0;
}
bool NetworkConfig::reset() {
    bool ok = true;
    for (auto it = routes_.rbegin(); it != routes_.rend(); ++it) {
        DWORD rc = DeleteIpForwardEntry2(&*it);
        if (rc != NO_ERROR && rc != ERROR_NOT_FOUND && rc != ERROR_FILE_NOT_FOUND) ok = false;
    }
    routes_.clear();
    for (auto it = addresses_.rbegin(); it != addresses_.rend(); ++it) {
        DWORD rc = DeleteUnicastIpAddressEntry(&*it);
        if (rc != NO_ERROR && rc != ERROR_NOT_FOUND && rc != ERROR_FILE_NOT_FOUND) ok = false;
    }
    addresses_.clear();
    if (filter_engine_) { if (FwpmEngineClose0(filter_engine_) != ERROR_SUCCESS) ok = false; filter_engine_ = nullptr; }
    luid_ = {};
    index_ = 0;
    return ok;
}
bool NetworkConfig::add_route(const Prefix& prefix, const NET_LUID& luid, const SOCKADDR_INET& next_hop, ULONG metric, std::wstring& detail) {
    if (routes_.size() >= 512) return fail(detail, L"Route limit", ERROR_BUFFER_OVERFLOW);
    MIB_IPFORWARD_ROW2 row{};
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid = luid;
    row.DestinationPrefix.Prefix = prefix.address;
    row.DestinationPrefix.PrefixLength = prefix.length;
    row.NextHop = next_hop;
    row.Metric = metric;
    row.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);
    row.Origin = NlroManual;
    if (luid.Value != luid_.Value) {
        // A crash must not leave permanent routes on the user's physical adapter.
        row.ValidLifetime = 300;
        row.PreferredLifetime = 300;
    }
    DWORD rc = CreateIpForwardEntry2(&row);
    if (rc == ERROR_OBJECT_ALREADY_EXISTS) return true; // Never take ownership of someone else's route.
    if (rc != NO_ERROR) return fail(detail, L"CreateIpForwardEntry2", rc);
    routes_.push_back(row);
    return true;
}
bool NetworkConfig::refresh_bypass_routes() {
    auto now = GetTickCount64();
    if (now - refreshed_at_ < 60000) return true;
    refreshed_at_ = now;
    for (auto& route : routes_) {
        if (route.InterfaceLuid.Value == luid_.Value) continue;
        route.ValidLifetime = 300;
        route.PreferredLifetime = 300;
        if (SetIpForwardEntry2(&route) != NO_ERROR) return false;
    }
    return true;
}
bool NetworkConfig::address(const char* ip, const char* mask, ADDRESS_FAMILY family, std::wstring& detail) {
    if (!ip || !*ip) return true;
    std::string raw(ip);
    if (auto slash = raw.find('/'); slash != std::string::npos) raw.resize(slash);
    SOCKADDR_INET addr{};
    if (!parse_address(raw, addr) || addr.si_family != family) return fail(detail, L"VPN address", ERROR_INVALID_DATA);
    unsigned prefix = family == AF_INET ? 32u : 128u;
    if (mask && *mask) {
        std::string route = family == AF_INET ? raw + "/" + mask : std::string(mask);
        Prefix parsed{};
        if (!parse_prefix(route, parsed) || parsed.address.si_family != family) return fail(detail, L"VPN subnet", ERROR_INVALID_DATA);
        prefix = parsed.length;
    }
    MIB_UNICASTIPADDRESS_ROW row{};
    InitializeUnicastIpAddressEntry(&row);
    row.InterfaceLuid = luid_;
    row.Address = addr;
    row.OnLinkPrefixLength = static_cast<UINT8>(prefix);
    row.PrefixOrigin = IpPrefixOriginManual;
    row.SuffixOrigin = IpSuffixOriginManual;
    row.ValidLifetime = 0xffffffff;
    row.PreferredLifetime = 0xffffffff;
    row.SkipAsSource = FALSE;
    row.DadState = IpDadStatePreferred;
    DWORD rc = CreateUnicastIpAddressEntry(&row);
    if (rc != NO_ERROR && rc != ERROR_OBJECT_ALREADY_EXISTS) return fail(detail, L"CreateUnicastIpAddressEntry", rc);
    if (rc == NO_ERROR) addresses_.push_back(row);
    return true;
}
static bool run_netsh(const std::wstring& arguments, HANDLE cancel, std::wstring& detail) {
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return fail(detail, L"GetSystemDirectory", GetLastError());
    std::wstring executable = std::wstring(system) + L"\\netsh.exe";
    std::wstring command = L"\"" + executable + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    // Only validated numeric interface indexes and IP literals enter this command.
    // No shell, script host, user-supplied command, or inherited credential handle is involved.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return fail(detail, L"DNS configuration", GetLastError());
    Handle child(process.hProcess), thread(process.hThread);
    HANDLE handles[2] = {child.get(), cancel};
    DWORD wait = WaitForMultipleObjects(cancel ? 2 : 1, handles, FALSE, 15000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), ERROR_CANCELLED);
        WaitForSingleObject(child.get(), 1000);
        return fail(detail, L"DNS configuration", wait == WAIT_OBJECT_0 + 1 ? ERROR_CANCELLED : ERROR_TIMEOUT);
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(child.get(), &code) || code != 0) return fail(detail, L"netsh DNS", code);
    return true;
}
bool NetworkConfig::dns(const oc_ip_info& info, HANDLE cancel, std::wstring& detail) {
    unsigned v4 = 0, v6 = 0;
    for (const char* value : info.dns) {
        if (!value || !*value) continue;
        SOCKADDR_INET parsed{};
        if (!parse_address(value, parsed)) return fail(detail, L"DNS address", ERROR_INVALID_DATA);
        unsigned& n = parsed.si_family == AF_INET ? v4 : v6;
        ++n;
        std::wstring args = parsed.si_family == AF_INET ? L"interface ipv4 " : L"interface ipv6 ";
        args += n == 1 ? L"set dnsservers " : L"add dnsservers ";
        args += L"name=" + std::to_wstring(index_);
        if (n == 1) args += L" source=static";
        args += L" address=" + wide(value);
        if (n > 1) args += L" index=" + std::to_wstring(n);
        args += L" validate=no";
        if (!run_netsh(args, cancel, detail)) return false;
    }
    // The interface is private to this connection; reconnects may remove an address family.
    if (!v4 && !run_netsh(L"interface ipv4 set dnsservers name=" + std::to_wstring(index_) + L" source=static address=none validate=no", cancel, detail)) return false;
    if (!v6 && !run_netsh(L"interface ipv6 set dnsservers name=" + std::to_wstring(index_) + L" source=static address=none validate=no", cancel, detail)) return false;
    return true;
}
bool NetworkConfig::filters(const Profile& profile, bool has_ipv6, const SOCKADDR_INET& peer, std::wstring& detail) {
    if (!profile.protect_dns && (!profile.block_ipv6 || has_ipv6)) return true;
    FWPM_SESSION0 session{};
    session.flags = wfp::dynamic_session; // WFP removes every filter if this process dies.
    session.displayData.name = const_cast<wchar_t*>(L"BridgeVPN session");
    DWORD rc = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &filter_engine_);
    if (rc != ERROR_SUCCESS) return fail(detail, L"FwpmEngineOpen", rc);
    GUID key{};
    RPC_STATUS uuid = UuidCreate(&key);
    if (uuid != RPC_S_OK && uuid != RPC_S_UUID_LOCAL_ONLY) return fail(detail, L"UuidCreate", uuid);
    FWPM_SUBLAYER0 sublayer{};
    sublayer.subLayerKey = key;
    sublayer.displayData.name = const_cast<wchar_t*>(L"BridgeVPN network protection");
    sublayer.weight = 0x7000;
    if ((rc = FwpmSubLayerAdd0(filter_engine_, &sublayer, nullptr)) != ERROR_SUCCESS) return fail(detail, L"FwpmSubLayerAdd", rc);
    auto add = [&](bool ipv6, UINT16 port, bool ipv6_block) {
        FWPM_FILTER_CONDITION0 conditions[4]{};
        UINT count = 0;
        UINT64 interface_id = luid_.Value;
        conditions[count].fieldKey = wfp::local_interface;
        conditions[count].matchType = FWP_MATCH_NOT_EQUAL;
        conditions[count].conditionValue.type = FWP_UINT64;
        conditions[count++].conditionValue.uint64 = &interface_id;
        conditions[count].fieldKey = wfp::flags;
        conditions[count].matchType = FWP_MATCH_FLAGS_NONE_SET;
        conditions[count].conditionValue.type = FWP_UINT32;
        conditions[count++].conditionValue.uint32 = FWP_CONDITION_FLAG_IS_LOOPBACK;
        if (!ipv6_block) {
            conditions[count].fieldKey = wfp::remote_port;
            conditions[count].matchType = FWP_MATCH_EQUAL;
            conditions[count].conditionValue.type = FWP_UINT16;
            conditions[count++].conditionValue.uint16 = port;
        }
        FWP_V6_ADDR_AND_MASK peer_address{};
        if (ipv6_block && peer.si_family == AF_INET6) {
            std::memcpy(peer_address.addr, &peer.Ipv6.sin6_addr, 16);
            peer_address.prefixLength = 128;
            conditions[count].fieldKey = wfp::remote_address;
            conditions[count].matchType = FWP_MATCH_NOT_EQUAL;
            conditions[count].conditionValue.type = FWP_V6_ADDR_MASK;
            conditions[count++].conditionValue.v6AddrMask = &peer_address;
        }
        FWPM_FILTER0 filter{};
        filter.displayData.name = const_cast<wchar_t*>(ipv6_block ? L"BridgeVPN IPv6 protection" : L"BridgeVPN DNS protection");
        filter.layerKey = ipv6 ? wfp::connect_v6 : wfp::connect_v4;
        filter.subLayerKey = key;
        filter.action.type = FWP_ACTION_BLOCK;
        filter.weight.type = FWP_UINT8;
        filter.weight.uint8 = 15;
        filter.numFilterConditions = count;
        filter.filterCondition = conditions;
        DWORD result = FwpmFilterAdd0(filter_engine_, &filter, nullptr, nullptr);
        return result == ERROR_SUCCESS || fail(detail, L"FwpmFilterAdd", result);
    };
    if (profile.protect_dns)
        for (UINT16 port : {static_cast<UINT16>(53), static_cast<UINT16>(853)})
            if (!add(false, port, false) || !add(true, port, false)) return false;
    if (profile.block_ipv6 && !has_ipv6 && !add(true, 0, true)) return false;
    return true;
}
bool NetworkConfig::apply(const char* interface_name, const oc_ip_info& info, const Profile& profile, HANDLE cancel, std::wstring& detail) {
    if (!reset()) return fail(detail, L"Previous network cleanup", ERROR_GEN_FAILURE);
    auto apply_all = [&]() -> bool {
        if (cancelled(cancel)) return fail(detail, L"Cancelled", ERROR_CANCELLED);
        if (!interface_name || !info.gateway_addr || (!info.addr && !info.addr6)) return fail(detail, L"Tunnel parameters", ERROR_INVALID_DATA);
        auto alias = wide(interface_name);
        DWORD rc = ConvertInterfaceAliasToLuid(alias.c_str(), &luid_);
        if (rc != NO_ERROR) return fail(detail, L"ConvertInterfaceAliasToLuid", rc);
        if ((rc = ConvertInterfaceLuidToIndex(&luid_, &index_)) != NO_ERROR) return fail(detail, L"ConvertInterfaceLuidToIndex", rc);
        SOCKADDR_INET peer{};
        if (!parse_address(info.gateway_addr, peer)) return fail(detail, L"VPN endpoint", ERROR_INVALID_DATA);

        // Snapshot physical paths before adding any VPN routes.
        struct Bypass { Prefix prefix; MIB_IPFORWARD_ROW2 route; };
        std::vector<Bypass> bypass;
        auto remember = [&](const Prefix& prefix) {
            MIB_IPFORWARD_ROW2 route{};
            SOCKADDR_INET source{};
            DWORD result = GetBestRoute2(nullptr, 0, nullptr, &prefix.address, 0, &route, &source);
            if (result != NO_ERROR) return fail(detail, L"GetBestRoute2", result);
            if (route.InterfaceLuid.Value == luid_.Value) return fail(detail, L"VPN endpoint routing loop", ERROR_INVALID_DATA);
            bypass.push_back({prefix, route});
            return true;
        };
        Prefix endpoint{peer, static_cast<UINT8>(peer.si_family == AF_INET ? 32 : 128)};
        if (!remember(endpoint)) return false;
        unsigned count = 0;
        for (auto route = info.split_excludes; route; route = route->next) {
            if (++count > 200 || !route->route) return fail(detail, L"Excluded routes", ERROR_INVALID_DATA);
            Prefix prefix{};
            if (!parse_prefix(route->route, prefix)) return fail(detail, L"Excluded route", ERROR_INVALID_DATA);
            if (!remember(prefix)) return false;
        }
        for (const auto& entry : bypass)
            if (!add_route(entry.prefix, entry.route.InterfaceLuid, entry.route.NextHop, 0, detail)) return false;

        for (ADDRESS_FAMILY family : {static_cast<ADDRESS_FAMILY>(AF_INET), static_cast<ADDRESS_FAMILY>(AF_INET6)}) {
            if ((family == AF_INET && !info.addr) || (family == AF_INET6 && !info.addr6)) continue;
            MIB_IPINTERFACE_ROW row{};
            InitializeIpInterfaceEntry(&row);
            row.Family = family;
            row.InterfaceLuid = luid_;
            if ((rc = GetIpInterfaceEntry(&row)) != NO_ERROR) return fail(detail, L"GetIpInterfaceEntry", rc);
            row.UseAutomaticMetric = FALSE;
            row.Metric = 5;
            row.NlMtu = static_cast<ULONG>(std::clamp(info.mtu, family == AF_INET6 ? 1280 : 576, 9000));
            row.SitePrefixLength = 0;
            row.DadTransmits = 0;
            if (family == AF_INET6) row.RouterDiscoveryBehavior = RouterDiscoveryDisabled;
            if ((rc = SetIpInterfaceEntry(&row)) != NO_ERROR) return fail(detail, L"SetIpInterfaceEntry", rc);
        }
        if (!address(info.addr, info.netmask, AF_INET, detail) || !address(info.addr6, info.netmask6, AF_INET6, detail)) return false;
        auto via_tunnel = [&](const Prefix& prefix) {
            if ((prefix.address.si_family == AF_INET && !info.addr) || (prefix.address.si_family == AF_INET6 && !info.addr6))
                return fail(detail, L"Route address family", ERROR_INVALID_DATA);
            SOCKADDR_INET hop{};
            hop.si_family = prefix.address.si_family;
            if (prefix.length != 0) return add_route(prefix, luid_, hop, 5, detail);
            // Two /1 routes avoid editing or deleting the user's existing default route.
            Prefix half = prefix;
            half.length = 1;
            if (!add_route(half, luid_, hop, 5, detail)) return false;
            if (half.address.si_family == AF_INET) reinterpret_cast<BYTE*>(&half.address.Ipv4.sin_addr)[0] = 0x80;
            else half.address.Ipv6.sin6_addr.u.Byte[0] = 0x80;
            return add_route(half, luid_, hop, 5, detail);
        };
        count = 0;
        for (auto route = info.split_includes; route; route = route->next) {
            if (++count > 200 || !route->route) return fail(detail, L"Included routes", ERROR_INVALID_DATA);
            Prefix prefix{};
            if (!parse_prefix(route->route, prefix) || !via_tunnel(prefix)) return false;
        }
        if (!info.split_includes) {
            Prefix prefix{};
            if (info.addr) { parse_prefix("0.0.0.0/0", prefix); if (!via_tunnel(prefix)) return false; }
            if (info.addr6) { parse_prefix("::/0", prefix); if (!via_tunnel(prefix)) return false; }
        }
        bool has_dns = false;
        for (const char* server : info.dns) {
            if (!server || !*server) continue;
            has_dns = true;
            SOCKADDR_INET address{};
            if (!parse_address(server, address)) return fail(detail, L"DNS address", ERROR_INVALID_DATA);
            if (same_address(address, peer)) return fail(detail, L"DNS must use the server's tunnel address, not its VPN endpoint", ERROR_INVALID_DATA);
            Prefix prefix{address, static_cast<UINT8>(address.si_family == AF_INET ? 32 : 128)};
            if (!via_tunnel(prefix)) return false;
        }
        if (profile.protect_dns && !has_dns) return fail(detail, L"Server must provide a VPN DNS address", ERROR_INVALID_DATA);
        if (cancelled(cancel)) return fail(detail, L"Cancelled", ERROR_CANCELLED);
        if (!dns(info, cancel, detail)) return false;
        if (!filters(profile, info.addr6 && *info.addr6, peer, detail)) return false;
        return true;
    };
    if (apply_all()) return true;
    if (!reset()) detail += L"; network cleanup was incomplete";
    return false;
}
}
