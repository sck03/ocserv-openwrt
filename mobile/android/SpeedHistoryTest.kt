package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class SpeedHistoryTest {
    @Test fun lowFrequencySamplesKeepTheTimeAxisAndStorageBounded() {
        val history = SpeedHistory(6)
        history.sample(1000, 1, 2)
        history.sample(6000, 10, 20)
        assertEquals(listOf(1L, 10L, 10L, 10L, 10L, 10L), history.snapshot().rx)
        history.sample(3_600_000, 30, 40)
        assertEquals(List(6) { 30L }, history.snapshot().rx)
        history.sample(3_600_900, 50, 60)
        assertEquals(50L, history.snapshot().rx.last())
        assertEquals(6, history.snapshot().rx.size)
        history.clear()
        assertTrue(history.snapshot().rx.isEmpty())
    }
    @Test fun aLateSampleCannotPublishAnEarlierConnection() {
        val history = SpeedHistory(8)
        history.sample(1000, 1, 2, 1000)
        history.clear(2000)
        history.sample(1500, 1000, 2000, 1000)
        assertTrue(history.snapshot().rx.isEmpty())
        history.sample(2000, 3, 4, 2000)
        assertEquals(2000L, history.snapshot().session)
        assertEquals(listOf(3L), history.snapshot().rx)
    }
}
