#pragma once
#include <glib.h>
// libnm's error-domain declaration needs explicit C linkage in C++ consumers.
extern "C" GQuark nm_secret_agent_error_quark(void);
#include <NetworkManager.h>
#include <nm-secret-agent-old.h>

GType linkora_agent_get_type();
NMSecretAgentOld *linkora_agent_new(GError **error);
void linkora_agent_select(NMSecretAgentOld *agent, const char *uuid);
void linkora_agent_cancel(NMSecretAgentOld *agent);
