package dev.opentunnel.vpn.service

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.Handler
import android.os.Looper
import dev.opentunnel.vpn.core.UpstreamCandidate
import dev.opentunnel.vpn.core.UpstreamPolicy
import java.io.Closeable

/** Main-thread callbacks own one monitor generation; disposed callbacks do nothing. */
internal class UpstreamMonitor(context: Context, private val changed: (Network?, Boolean) -> Unit) : Closeable {
    private val manager = checkNotNull(context.getSystemService(ConnectivityManager::class.java))
    private val handler = Handler(Looper.getMainLooper())
    private val networks = mutableMapOf<Long, Network>()
    private val capabilities = mutableMapOf<Long, NetworkCapabilities>()
    private val links = mutableMapOf<Long, String>()
    private var preferred: Long? = null
    private var selected: Long? = null
    private var signature: String? = null
    private var initialized = false
    private var closed = false
    private var defaultRegistered = false
    private val bestMatching = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
    private val publishTask = Runnable { publish() }
    private fun schedule() {
        handler.removeCallbacks(publishTask)
        handler.postDelayed(publishTask, 150)
    }

    private fun publish() {
        if (closed) return
        val candidates = capabilities.mapNotNull { (id, caps) ->
            if (id !in links || !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) ||
                caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) null
            else UpstreamCandidate(id, caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED), when {
                caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> 2
                caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> 1
                else -> 0
            })
        }
        val next = UpstreamPolicy.choose(candidates, preferred, selected)
        val properties = next?.let { links[it] }
        if (next == selected && properties == signature) return
        val reconnect = initialized
        selected = next
        signature = properties
        if (next != null) initialized = true
        changed(next?.let { networks[it] }, reconnect)
    }

    private val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            if (closed) return
            if (bestMatching) {
                // Android can stop reporting the previous best network without
                // delivering its eventual loss. Keep only the current choice.
                networks.clear(); capabilities.clear(); links.clear()
                preferred = network.networkHandle
            }
            networks[network.networkHandle] = network
        }
        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
            if (closed || network.networkHandle !in networks) return
            capabilities[network.networkHandle] = caps
            schedule()
        }
        override fun onLinkPropertiesChanged(network: Network, properties: LinkProperties) {
            if (closed || network.networkHandle !in networks) return
            links[network.networkHandle] = listOf(properties.interfaceName, properties.mtu.toString(),
                properties.linkAddresses.map { it.toString() }.sorted().joinToString(),
                properties.dnsServers.map { it.hostAddress.orEmpty() }.sorted().joinToString(),
                properties.routes.map { it.toString() }.sorted().joinToString()).joinToString("|")
            schedule()
        }
        override fun onLost(network: Network) {
            if (closed) return
            val id = network.networkHandle
            networks.remove(id); capabilities.remove(id); links.remove(id)
            if (preferred == id) preferred = null
            schedule()
        }
    }
    private val defaultCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
            if (closed) return
            preferred = network.networkHandle.takeUnless { caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN) }
            schedule()
        }
        override fun onLost(network: Network) {
            if (closed) return
            if (preferred == network.networkHandle) { preferred = null; schedule() }
        }
    }
    init {
        val request = NetworkRequest.Builder().addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN).build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) manager.registerBestMatchingNetworkCallback(request, callback, handler)
        else {
            manager.registerNetworkCallback(request, callback, handler)
            try {
                manager.registerDefaultNetworkCallback(defaultCallback, handler)
                defaultRegistered = true
            } catch (error: Exception) {
                manager.unregisterNetworkCallback(callback)
                throw error
            }
        }
    }
    override fun close() {
        if (closed) return
        closed = true
        handler.removeCallbacks(publishTask)
        runCatching { manager.unregisterNetworkCallback(callback) }
        if (defaultRegistered) runCatching { manager.unregisterNetworkCallback(defaultCallback) }
        networks.clear(); capabilities.clear(); links.clear()
    }
}
