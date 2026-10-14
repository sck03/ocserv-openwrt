#pragma once
#include <glib.h>
// libnm's error-domain declaration needs explicit C linkage in C++ consumers.
extern "C" GQuark nm_secret_agent_error_quark(void);
#include <NetworkManager.h>
#include <nm-secret-agent-old.h>
#include <string>

std::string lansyra_auth_dialog();
GType lansyra_agent_get_type();
NMSecretAgentOld *lansyra_agent_new(GError **error);
void lansyra_agent_select(NMSecretAgentOld *agent, const char *uuid);
void lansyra_agent_cancel(NMSecretAgentOld *agent);
