package dev.opentunnel.vpn.core

/** Zero means no timer and no JNI statistics request. */
internal object StatisticsPolicy {
    fun interval(interactive: Boolean, visible: Boolean, notification: Boolean, widgets: Boolean): Long = when {
        interactive && visible -> 1_000L
        notification -> if (interactive) 2_000L else 5_000L
        interactive && widgets -> 5_000L
        else -> 0L
    }
}
