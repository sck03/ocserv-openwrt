package dev.opentunnel.vpn.data

import java.io.Closeable
import java.io.File
import java.io.InputStream
import java.util.UUID

/** Immutable private files, retained by saved profiles and live native sessions. */
class CertificateFiles(
    directory: File,
    private val protectFile: (File) -> Unit = { target ->
        check(target.setReadable(false, false) && target.setWritable(false, false) &&
            target.setReadable(true, true) && target.setWritable(true, true)) { "Cannot protect certificate file" }
    },
    private val openContent: (String) -> InputStream,
) {
    private val directory = directory.canonicalFile
    private val live = mutableMapOf<String, Int>()
    private var saved = emptySet<String>()
    private var reconciled = false
    private val filename = Regex("[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\\.pem")

    class Lease internal constructor(val profile: VpnProfile, private var release: (() -> Unit)?) : Closeable {
        @Synchronized override fun close() { release?.invoke(); release = null }
    }

    private fun paths(profile: VpnProfile) =
        listOf(profile.caCertPath, profile.userCertPath, profile.privateKeyPath).filter { it.isNotEmpty() }.toSet()

    private fun file(path: String): File {
        val candidate = File(path)
        require(filename.matches(candidate.name) && candidate.absoluteFile == candidate.canonicalFile &&
            candidate.parentFile?.canonicalFile == directory.canonicalFile) { "Select a certificate from the file picker" }
        check(candidate.isFile) { "Certificate file is unavailable; select it again" }
        return candidate
    }

    @Synchronized fun retain(profile: VpnProfile): Lease {
        val retained = paths(profile)
        retained.forEach(::file)
        retained.forEach { live[it] = (live[it] ?: 0) + 1 }
        return Lease(profile) { release(retained) }
    }

    @Synchronized private fun release(paths: Set<String>) {
        paths.forEach { path ->
            val count = checkNotNull(live[path]) - 1
            if (count == 0) live.remove(path) else live[path] = count
        }
        prune()
    }

    /** Stage only picker URIs or existing managed files; failures remove all new copies. */
    @Synchronized fun stage(profile: VpnProfile, ca: ByteArray? = null): Lease {
        val created = mutableListOf<File>()
        fun copy(input: InputStream): String {
            check(directory.isDirectory || directory.mkdirs()) { "Cannot create certificate storage" }
            val target = File(directory, UUID.randomUUID().toString() + ".pem")
            created += target
            input.use {
                check(target.createNewFile()) { "Cannot create certificate file" }
                protectFile(target)
                target.outputStream().use { output ->
                    val buffer = ByteArray(8192)
                    var total = 0
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        check(count > 0) { "Certificate provider returned no data" }
                        total += count
                        require(total <= MAX_BYTES) { "Certificate file exceeds 1 MiB" }
                        output.write(buffer, 0, count)
                    }
                    require(total > 0) { "Certificate file is empty" }
                    output.fd.sync()
                }
            }
            return target.absolutePath
        }
        fun import(source: String, caFile: Boolean = false): String = when {
            source.isEmpty() -> ""
            source.startsWith("content://") -> openContent(source).use { input ->
                if (caFile) copy(PublicCa.validate(ConnectionFile.read(input, 65536)).inputStream()) else copy(input)
            }
            else -> file(source).absolutePath
        }
        return try {
            retain(profile.copy(caCertPath = if (ca == null) import(profile.caCertPath, true) else copy(PublicCa.validate(ca).inputStream()),
                userCertPath = import(profile.userCertPath), privateKeyPath = import(profile.privateKeyPath)))
        } catch (error: Throwable) {
            created.forEach { it.delete() }
            throw error
        }
    }

    /** Call after a successful profile transaction, while profile mutations are serialized. */
    @Synchronized fun reconcile(profiles: List<VpnProfile>) {
        val references = profiles.flatMap(::paths).toSet()
        if (!reconciled || references != saved) {
            saved = references
            reconciled = true
            prune()
        }
    }

    private fun prune() {
        directory.listFiles()?.forEach { candidate ->
            if (filename.matches(candidate.name) && candidate.isFile &&
                candidate.absolutePath !in saved && candidate.absolutePath !in live) candidate.delete()
        }
    }

    companion object { const val MAX_BYTES = 1024 * 1024 }
}
