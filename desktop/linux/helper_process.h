#pragma once
#include <gio/gio.h>

// Main-context-only shutdown. The callback runs after the child has exited and
// all pipe operations have finished; the caller may release its own process ref.
void lansyra_stop_helper(GSubprocess *process, bool graceful, void (*done)(gpointer), gpointer data);
