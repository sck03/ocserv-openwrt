package dev.opentunnel.vpn.data

import org.junit.Assert.*
import org.junit.Test

class ProfileCodecTest {
    private val profile = VpnProfile(id = "00000000-0000-4000-8000-000000000001", server = "vpn.example.com", password = "fixture-password", tokenString = "fixture-token")
    private val codec = ProfileCodec({ "encrypted:$it" }, { require(it.startsWith("encrypted:")); it.removePrefix("encrypted:") })

    @Test fun credentialsRoundTripAndCorruptDataCannotBecomeAnEmptyStore() {
        val original = codec.encode(listOf(profile))
        assertEquals(listOf(profile), codec.decode(original))
        for (invalid in listOf("{broken", original.replace("encrypted:", "invalid:"), original + "x"))
            assertTrue(runCatching { codec.decode(invalid) }.isFailure)
        assertTrue(ProfileCodec({ "" }, { "" }).runCatching { encode(listOf(profile)) }.isFailure)
        assertTrue(ProfileCodec({ "" }, { "" }).runCatching { decode(original) }.isFailure)
    }

    @Test fun aggregateSizeIsCheckedAfterEncryptionBeforeStorage() {
        val profiles = (1..128).map { profile.copy(id = "00000000-0000-4000-8000-" + it.toString().padStart(12, '0'), password = "x".repeat(8192), tokenString = "y".repeat(8192)) }
        ProfilePolicy.validate(profiles)
        assertTrue(runCatching { codec.encode(profiles) }.isFailure)
    }

    @Test fun unrememberedPasswordIsNotPersisted() {
        val roundTrip = codec.decode(codec.encode(listOf(profile.copy(savePassword = false)))).single()
        assertEquals("", roundTrip.password)
        assertEquals(profile.tokenString, roundTrip.tokenString)
    }
}
