#include "connection_controller.h"
#include <iostream>
#include <stdexcept>

// Only the credential agent is replaced; the controller and libnm object types are real.
NMSecretAgentOld *lansyra_agent_new(GError **) { return nullptr; }
void lansyra_agent_cancel(NMSecretAgentOld *) {}
void lansyra_agent_select(NMSecretAgentOld *, const char *) {}
namespace lansyra {
struct ConnectionControllerTest {
    static uint64_t begin(ConnectionController &controller) {
        ++controller.pending_;
        controller.transition("connecting");
        return controller.request_.begin();
    }
    static void complete(ConnectionController &controller, uint64_t token) {
        --controller.pending_;
        controller.activation_finished(token, nullptr);
    }
};
}
int main() {
    using namespace lansyra;
    auto check = [](bool value) { if (!value) throw std::runtime_error("activation lifecycle regression"); };
    auto *client = NM_CLIENT(g_object_new(NM_TYPE_CLIENT, nullptr));
    unsigned notifications = 0;
    ConnectionController controller(client, [&] { ++notifications; }, nullptr);
    g_object_unref(client);
    auto token = ConnectionControllerTest::begin(controller);
    controller.cancel();
    check(controller.busy() && controller.state().phase == "disconnecting");
    ConnectionControllerTest::complete(controller, token);
    check(!controller.busy() && controller.state().phase == "idle");
    token = ConnectionControllerTest::begin(controller);
    ConnectionControllerTest::complete(controller, token);
    check(!controller.busy() && controller.state().phase == "failed" && controller.state().error == "configuration");
    token = ConnectionControllerTest::begin(controller);
    controller.cancel();
    const auto newer = ConnectionControllerTest::begin(controller);
    ConnectionControllerTest::complete(controller, token);
    check(controller.busy() && controller.state().phase == "connecting");
    controller.cancel();
    ConnectionControllerTest::complete(controller, newer);
    check(!controller.busy() && controller.state().phase == "idle" && notifications >= 6);
    std::cout << "Linux cancellation, null activation and superseded callback checks passed\n";
}
