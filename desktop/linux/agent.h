#pragma once
#include <NetworkManager.h>

NMSecretAgentOld *linkora_agent_new(GError **error);
void linkora_agent_select(NMSecretAgentOld *agent, const char *uuid);
void linkora_agent_cancel(NMSecretAgentOld *agent);
