package dev.opentunnel.vpn.core

/** A missing allowlist must never fall through to Android's all-app default. */
internal object AppRoutingPolicy {
    fun apply(enabled: Boolean, includeSelected: Boolean, packages: Set<String>, add: (String) -> Boolean): Int {
        if (!enabled) return 0
        val applied = packages.count(add)
        check(!includeSelected || applied > 0) {
            "No selected application is installed. Select at least one app before connecting."
        }
        return applied
    }
}
