#include "helper_process.h"

namespace {
struct Shutdown {
    GSubprocess *process;
    GCancellable *cancel;
    void (*done)(gpointer);
    gpointer data;
    guint timeout = 0;
    unsigned pending;
    bool exited = false;

    void release() {
        if (--pending)
            return;
        done(data);
        g_object_unref(cancel);
        g_object_unref(process);
        delete this;
    }
    void abort() {
        g_cancellable_cancel(cancel);
        if (!exited)
            g_subprocess_force_exit(process);
    }
};
} // namespace

void lansyra_stop_helper(GSubprocess *process, bool graceful, void (*done)(gpointer), gpointer data) {
    auto *shutdown = new Shutdown{G_SUBPROCESS(g_object_ref(process)), g_cancellable_new(), done, data,
                                  0, graceful ? 2u : 1u};
    if (graceful) {
        // The helper has already returned its reply. Only teardown gets a short
        // deadline; interactive authentication itself remains user-controlled.
        shutdown->timeout = g_timeout_add(1000, [](gpointer data) -> gboolean {
            auto *shutdown = static_cast<Shutdown *>(data);
            shutdown->timeout = 0;
            shutdown->abort();
            return G_SOURCE_REMOVE;
        }, shutdown);
        g_output_stream_write_all_async(
            g_subprocess_get_stdin_pipe(process), "QUIT\n", 5, G_PRIORITY_DEFAULT, shutdown->cancel,
            [](GObject *stream, GAsyncResult *result, gpointer data) {
                auto *shutdown = static_cast<Shutdown *>(data);
                if (!g_output_stream_write_all_finish(G_OUTPUT_STREAM(stream), result, nullptr, nullptr))
                    shutdown->abort();
                g_output_stream_close_async(G_OUTPUT_STREAM(stream), G_PRIORITY_DEFAULT, nullptr,
                    [](GObject *stream, GAsyncResult *result, gpointer data) {
                        g_output_stream_close_finish(G_OUTPUT_STREAM(stream), result, nullptr);
                        static_cast<Shutdown *>(data)->release();
                    }, shutdown);
            }, shutdown);
    } else
        shutdown->abort();
    // Never cancel the wait: completion is the ownership barrier for the child.
    g_subprocess_wait_async(process, nullptr, [](GObject *process, GAsyncResult *result, gpointer data) {
        auto *shutdown = static_cast<Shutdown *>(data);
        g_subprocess_wait_finish(G_SUBPROCESS(process), result, nullptr);
        shutdown->exited = true;
        if (shutdown->timeout)
            g_source_remove(shutdown->timeout);
        g_cancellable_cancel(shutdown->cancel);
        shutdown->release();
    }, shutdown);
}
