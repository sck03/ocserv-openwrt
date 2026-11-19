package dev.opentunnel.vpn.data

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermission
import org.junit.Assume.assumeTrue

class CertificateFilesTest {
    private val certificate = requireNotNull(javaClass.getResource("/public-profiles/ca.pem.fixture")).readBytes()
    private val protect: (File) -> Unit = { assertEquals(0L, it.length()) }
    private fun fixture(test: (File) -> Unit) {
        val directory = Files.createTempDirectory("certificate-files").toFile()
        try { test(directory) } finally { directory.deleteRecursively() }
    }

    @Test fun pickerCopiesSurviveProviderClosureAndLiveSessionOutlivesDeletion() = fixture { directory ->
        var closed = false
        val files = CertificateFiles(directory, protect) {
            object : ByteArrayInputStream(certificate) { override fun close() { closed = true } }
        }
        val staged = files.stage(VpnProfile(caCertPath = "content://picker/ca"))
        val path = File(staged.profile.caCertPath)
        assertTrue(closed)
        assertArrayEquals(certificate, path.readBytes())
        files.reconcile(listOf(staged.profile))
        staged.close()
        val session = files.retain(staged.profile)
        files.reconcile(emptyList())
        assertTrue(path.exists())
        session.close()
        session.close()
        assertFalse(path.exists())
    }

    @Test fun failedSaveAndOversizedOrEmptyImportsLeaveNoFiles() = fixture { directory ->
        val files = CertificateFiles(directory, protect) { source ->
            ByteArrayInputStream(when (source) {
                "content://large" -> ByteArray(CertificateFiles.MAX_BYTES + 1)
                "content://empty" -> ByteArray(0)
                else -> certificate
            })
        }
        files.stage(VpnProfile(caCertPath = "content://valid")).close()
        assertTrue(directory.listFiles()!!.isEmpty())
        for (source in listOf("content://large", "content://empty")) {
            assertThrows(IllegalArgumentException::class.java) {
                files.stage(VpnProfile(caCertPath = "content://valid", privateKeyPath = source))
            }
            assertTrue(directory.listFiles()!!.isEmpty())
        }
    }

    @Test fun externalPathsAndUnmanagedFilesAreNeverAcceptedOrRemoved() = fixture { directory ->
        val unrelated = File(directory, "manual.pem").apply { writeText("keep") }
        val files = CertificateFiles(directory) { error("must not open provider") }
        assertThrows(IllegalArgumentException::class.java) { files.stage(VpnProfile(caCertPath = unrelated.path)) }
        files.reconcile(emptyList())
        assertEquals("keep", unrelated.readText())
    }

    @Test fun failedPermissionsAbortBeforeWritingAndCloseTheProvider() = fixture { directory ->
        var closed = false
        val files = CertificateFiles(directory, { throw IllegalStateException("permission failure") }) {
            object : ByteArrayInputStream("private key".toByteArray()) { override fun close() { closed = true } }
        }
        assertThrows(IllegalStateException::class.java) { files.stage(VpnProfile(privateKeyPath = "content://key")) }
        assertTrue(closed)
        assertTrue(directory.listFiles()!!.isEmpty())
    }

    @Test fun privateFilesUseOwnerOnlyPermissionsOnPosix() = fixture { directory ->
        assumeTrue(Files.getFileStore(directory.toPath()).supportsFileAttributeView("posix"))
        val files = CertificateFiles(directory) { ByteArrayInputStream(certificate) }
        files.stage(VpnProfile(caCertPath = "content://ca")).use { staged ->
            assertEquals(setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE),
                Files.getPosixFilePermissions(File(staged.profile.caCertPath).toPath()))
        }
    }
}
