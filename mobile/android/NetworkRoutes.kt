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
               bypassLocal: Boolean, ipv6: Boolean, dns: Collection<String> = emptyList()): List<Cidr> {
        val custom = if (rules.enabled) parse(rules.entries, 128) else emptyList()
        val server = parse(serverIncludes, 4096)
        val included = when {
            rules.enabled && rules.includeSelected -> {
                require(custom.isNotEmpty()) { "Select at least one IP address or CIDR network" }
                custom
            }
            server.isNotEmpty() -> buildList {
                addAll(server)
                // OpenConnect's include lists are independent for each family.
                if (server.none { !it.isIpv6 }) add(Cidr("0.0.0.0", 0, false))
                if (server.none { it.isIpv6 }) add(Cidr("::", 0, true))
            }
            else -> listOf(Cidr("0.0.0.0", 0, false), Cidr("::", 0, true))
        }
        val negotiated = included.filter { ipv6 || !it.isIpv6 }
        require(negotiated.isNotEmpty()) { "The selected routes require an address family not assigned by the server" }
        val excluded = buildList {
            addAll(parse(serverExcludes, 4096))
            // Explicit remote networks take precedence over the convenience LAN
            // bypass. Server/user exclusions remain authoritative.
            if (bypassLocal) addAll(RoutePolicy.routes(Net.LOCAL_NETWORKS,
                negotiated.filter { it.prefixLength > 0 }))
            if (rules.enabled && !rules.includeSelected) addAll(custom)
        }
        val traffic = RoutePolicy.routes(negotiated, excluded.filter { ipv6 || !it.isIpv6 })
        require(traffic.isNotEmpty()) { "The routing rules exclude every selected network" }
        // Android's VPN resolver needs explicit reachability even when its DNS
        // address falls inside a bypassed private network.
        val resolvers = parse(dns, 3).filter { ipv6 || !it.isIpv6 }.map {
            it.copy(prefixLength = if (it.isIpv6) 128 else 32)
        }
        return RoutePolicy.routes(traffic + resolvers, emptyList())
    }
}
