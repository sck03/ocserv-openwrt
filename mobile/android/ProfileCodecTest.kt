package dev.opentunnel.vpn.data

import org.junit.Assert.*
import org.junit.Test

class ProfileCodecTest {
    private val profile = VpnProfile(id = "00000000-0000-4000-8000-000000000001", server = "vpn.example.com",
        password = "fixture-password", softwareTokenMode = 2, tokenString = "fixture-token")
    private var encrypted = 0
    private var decrypted = 0
    private val codec = ProfileCodec({ value, context -> encrypted++; "$context:$value" },
        { value, context -> decrypted++; require(value.startsWith("$context:")); value.removePrefix("$context:") })

    @Test fun metadataAndReorderNeverUnlockOrReencryptCredentials() {
        val stored = codec.update(profile, null)
        val decoded = codec.decode(codec.encode(listOf(stored)))
        assertEquals("", decoded.single().profile.password)
        assertEquals(2, encrypted)
        assertEquals(0, decrypted)
        val reordered = ProfilePolicy.reorder(decoded, listOf(profile.id)) { it.profile.id }
        assertEquals(stored, reordered.single())
        assertEquals(profile, codec.unlock(reordered.single()))
        assertEquals(2, decrypted)
    }

    @Test fun corruptAndLegacyStoresFailClosed() {
        for (invalid in listOf("{broken", " ", "[]", "{\"schema\":1,\"profiles\":[]}", "{\"schema\":2,\"profiles\":[]}"))
            assertTrue(runCatching { codec.decode(invalid) }.isFailure)
        assertTrue(ProfileCodec({ _, _ -> "" }, { _, _ -> "" }).runCatching { update(profile, null) }.isFailure)
        val stored = codec.update(profile, null)
        assertTrue(ProfileCodec({ _, _ -> "" }, { _, _ -> "" }).runCatching { unlock(stored) }.isFailure)
        assertTrue(runCatching { codec.encode(listOf(stored.copy(profile = profile))) }.isFailure)
    }

    @Test fun editsRetainSecretsButOriginChangesAndExplicitDisableClearThem() {
        val stored = codec.update(profile, null)
        assertEquals(stored, codec.update(stored.profile, stored))
        assertEquals(2, encrypted)
        val changed = codec.update(stored.profile.copy(server = "other.example.com"), stored)
        assertEquals("", changed.password)
        assertEquals("", changed.token)
        assertEquals("", codec.update(stored.profile.copy(savePassword = false), stored).password)
        assertEquals("", codec.update(stored.profile.copy(softwareTokenMode = 0), stored).token)
        assertTrue(runCatching { codec.unlock(stored.copy(profile = stored.profile.copy(server = "other.example.com"))) }.isFailure)
    }

    @Test fun reorderUsesLatestObjectsAndDoesNotResurrectDeletedProfiles() {
        val latest = profile.copy(name = "latest")
        val added = profile.copy(id = "00000000-0000-4000-8000-000000000002")
        assertEquals(listOf(latest, added), ProfilePolicy.reorder(listOf(latest, added), listOf("deleted", profile.id)) { it.id })
        assertTrue(runCatching { ProfilePolicy.reorder(listOf(profile), listOf(profile.id, profile.id)) { it.id } }.isFailure)
    }

    @Test fun aggregateSizeIsCheckedBeforeStorage() {
        val profiles = (1..128).map { codec.update(profile.copy(id = "00000000-0000-4000-8000-" +
            it.toString().padStart(12, '0'), password = "x".repeat(8192), tokenString = "y".repeat(8192)), null) }
        assertTrue(runCatching { codec.encode(profiles) }.isFailure)
    }
}
