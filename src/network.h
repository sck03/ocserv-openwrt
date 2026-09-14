#pragma once
#include "common.h"
#include <openconnect.h>

namespace bridge {
class NetworkConfig {
public:
    NetworkConfig() = default;
    ~NetworkConfig() { reset(); }
    NetworkConfig(const NetworkConfig&) = delete;
    NetworkConfig& operator=(const NetworkConfig&) = delete;
    bool apply(const char* interface_name, const oc_ip_info& info, const Profile& profile, HANDLE cancel, std::wstring& detail);
    bool reset();
    bool refresh_bypass_routes();
    NET_LUID luid() const { return luid_; }
private:
    bool add_route(const Prefix& prefix, const NET_LUID& luid, const SOCKADDR_INET& next_hop, ULONG metric, std::wstring& detail);
    bool address(const char* ip, const char* mask, ADDRESS_FAMILY family, std::wstring& detail);
    bool dns(const oc_ip_info& info, HANDLE cancel, std::wstring& detail);
    bool filters(const Profile& profile, bool has_ipv6, const SOCKADDR_INET& peer, std::wstring& detail);
    NET_LUID luid_{};
    NET_IFINDEX index_ = 0;
    std::vector<MIB_IPFORWARD_ROW2> routes_;
    std::vector<MIB_UNICASTIPADDRESS_ROW> addresses_;
    HANDLE filter_engine_ = nullptr;
    ULONGLONG refreshed_at_ = 0;
};
}
