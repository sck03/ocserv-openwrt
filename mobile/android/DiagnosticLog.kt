package dev.opentunnel.vpn.core

import java.net.URI
import java.net.URLDecoder
import java.net.URLEncoder
import java.util.Base64
import java.util.Locale

/** Sanitize before both Logcat and UI publication; bounds apply to characters and entries. */
internal class DiagnosticLog {
    private val secrets = LinkedHashSet<String>()
    private var secretCharacters = 0
    private var saturated = false
    private val urls = Regex("(?i)https?://[^\\s<>\"']+")
    private val fields = Regex("(?i)(password|passwd|credential|passcode|cookie|webvpn|token|secret|authorization|samlresponse|relaystate|username|authgroup|sessionid)[\\s\"']*[:=]|<(auth|form|input|select|option)|BEGIN.*PRIVATE KEY")

    @Synchronized fun remember(value: String?) {
        if (value.isNullOrEmpty() || saturated) return
        if (value.length > 8192) { saturated = true; return }
        val variants = listOf(value, URLEncoder.encode(value, "UTF-8"),
            Base64.getEncoder().encodeToString(value.toByteArray(Charsets.UTF_8)),
            value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;").replace("'", "&apos;"),
            value.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t"))
        for (item in variants) {
            val folded = item.lowercase(Locale.ROOT)
            if (folded in secrets) continue
            if (secrets.size >= 1024 || secretCharacters + folded.length > 262144) {
                saturated = true
                break
            }
            secrets.add(folded)
            secretCharacters += folded.length
        }
    }

    @Synchronized fun clean(message: String): String {
        if (message.length > 8192) return "[oversized log entry omitted]"
        if (saturated) return "[authentication data redacted]"
        var probe = message
        repeat(2) { probe = runCatching { URLDecoder.decode(probe, "UTF-8") }.getOrDefault(probe) }
        val folded = probe.lowercase(Locale.ROOT)
        val plain = message.lowercase(Locale.ROOT)
        if (fields.containsMatchIn(probe) || secrets.any { folded.contains(it) || plain.contains(it) })
            return "[authentication data redacted]"
        return urls.replace(message) { match ->
            runCatching {
                val url = URI(match.value)
                if (url.rawUserInfo != null) "[authentication data redacted]"
                else "${url.scheme}://${url.rawAuthority}/[redacted]"
            }.getOrDefault("[URL redacted]")
        }.map { if (it < ' ' && it != '\n' && it != '\t') ' ' else it }.joinToString("")
    }

    @Synchronized fun clear() {
        secrets.clear()
        secretCharacters = 0
        saturated = false
    }
}
