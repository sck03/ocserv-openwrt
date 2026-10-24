package dev.opentunnel.vpn.util

/** Numeric routing only: DNS search suffixes never represent traffic selectors. */
internal object NetworkRoutes {
    data class Rules(val enabled: Boolean, val includeSelected: Boolean, val entries: Collection<String>)

    private fun parse(values: Collection<String>, maximum: Int): List<Cidr> {
        require(values.size <= maximum) { "Too many network routes" }
        return values.map { value ->
            requireNotNull(Net.parseCidr(value)) { "Use IP addresses or CIDR networks; domain and wildcard routing is unsupported" }
        }
    }

    fun routes(serverIncludes: Collection<String>, serverExcludes: Collection<String>, rules: Rules,
               bypassLocal: Boolean, ipv6: Boolean): List<Cidr> {
        val custom = if (rules.enabled) parse(rules.entries, 128) else emptyList()
        val server = parse(serverIncludes, 4096)
        val included = when {
            rules.enabled && rules.includeSelected -> {
                require(custom.isNotEmpty()) { "Select at least one IP address or CIDR network" }
                custom
            }
            server.isNotEmpty() -> server
            else -> listOf(Cidr("0.0.0.0", 0, false), Cidr("::", 0, true))
        }
        val excluded = buildList {
            addAll(parse(serverExcludes, 4096))
            if (bypassLocal) addAll(Net.LOCAL_NETWORKS)
            if (rules.enabled && !rules.includeSelected) addAll(custom)
        }
        val negotiated = included.filter { ipv6 || !it.isIpv6 }
        require(negotiated.isNotEmpty()) { "The selected routes require an address family not assigned by the server" }
        return RoutePolicy.routes(negotiated, excluded.filter { ipv6 || !it.isIpv6 })
    }
}
