package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class StatisticsPolicyTest {
    @Test fun hiddenConsumersDoNotKeepTheStatisticsTimerAlive() {
        assertEquals(0L, StatisticsPolicy.interval(true, false, false, false))
        assertEquals(0L, StatisticsPolicy.interval(false, true, false, true))
    }
    @Test fun eachVisibleConsumerGetsItsRequiredCadence() {
        assertEquals(1000L, StatisticsPolicy.interval(true, true, true, true))
        assertEquals(2000L, StatisticsPolicy.interval(true, false, true, true))
        assertEquals(5000L, StatisticsPolicy.interval(false, false, true, false))
        assertEquals(5000L, StatisticsPolicy.interval(true, false, false, true))
    }
}
