package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class HotpCounterTest {
    @Test fun reservesCountersWithoutChangingSecretsOrOtherParameters() {
        assertEquals("base32:GEZDGNBV,8", HotpCounter.next("base32:GEZDGNBV,7"))
        assertEquals("0x01020304,1", HotpCounter.next("0x01020304,0"))
        assertEquals("otpauth://hotp/test?secret=GEZDGNBV&counter=42&digits=6",
            HotpCounter.next("otpauth://hotp/test?secret=GEZDGNBV&counter=41&digits=6"))
    }
    @Test fun refusesMissingAmbiguousAndOverflowingCounters() {
        for (value in listOf("secret", "base32:AA", "base32:AA,-1", "base32:AA,2147483647",
            "otpauth://hotp/test?secret=AA", "otpauth://hotp/test?counter=1&counter=2"))
            assertThrows(value, IllegalArgumentException::class.java) { HotpCounter.next(value) }
    }
}
