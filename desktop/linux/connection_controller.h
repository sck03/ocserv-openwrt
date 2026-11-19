#pragma once
#include "agent.h"
#include "protocol.h"
#include <functional>

namespace lansyra {
struct ConnectionState {
    std::string phase = "idle", error = "none", profile;
    uint64_t session = 0;
    unsigned attempt = 1, retry_seconds = 0;
};

// NetworkManager lifetime and cancellation are independent of GTK widgets.
class ConnectionController {
public:
    ConnectionController(NMClient *client, std::function<void()> changed, GError **error);
    ~ConnectionController();
    bool ready() const;
    bool initialized() const {
        return agent_ != nullptr;
    }
    bool busy() const {
        return request_.wanted || active_ || pending_;
    }
    const ConnectionState &state() const {
        return state_;
    }
    void start(const std::string &profile, bool retry);
    void cancel();
    bool restore(bool retry);

private:
    void transition(const char *phase);
    void clear_active();
    void deactivate();
    void activate();
    void ended(NMActiveConnection *source = nullptr);
    static void active_changed(NMActiveConnection *, guint, guint, gpointer);
    static void vpn_changed(NMVpnConnection *, guint, guint, gpointer);
    NMClient *client_;
    NMSecretAgentOld *agent_ = nullptr;
    NMActiveConnection *active_ = nullptr;
    Generation request_;
    ConnectionState state_;
    std::function<void()> changed_;
    unsigned pending_ = 0;
    guint retry_timer_ = 0;
    bool retry_ = false, deactivating_ = false;
};
} // namespace lansyra
