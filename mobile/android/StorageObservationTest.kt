package dev.opentunnel.vpn.data

import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class StorageObservationTest {
    @Test fun transientReadFailureRecoversWithoutLosingSubscribers() = runTest {
        var attempts = 0
        val values = mutableListOf<Int>()
        val source = flow { if (++attempts == 1) throw IOException(); emit(42) }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            StorageObservation().observe(source) { -1 }.collect { values += it }
        }
        advanceTimeBy(250); runCurrent()
        assertEquals(listOf(-1, 42), values)
        assertEquals(2, attempts)
    }
    @Test fun persistentErrorsStopRetryingAndExplicitReloadRestartsThem() = runTest {
        var attempts = 0
        var recovered = false
        val observation = StorageObservation()
        var value = 0
        val source = flow { attempts++; if (!recovered) throw IOException(); emit(42) }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            observation.observe(source) { -1 }.collect { value = it }
        }
        advanceTimeBy(10000); runCurrent()
        assertEquals(4, attempts)
        recovered = true; observation.reload(); runCurrent()
        assertEquals(42, value)
        assertEquals(5, attempts)
    }
    @Test fun invalidDataNeedsReloadAndCancellationIsNeverAnErrorValue() = runTest {
        var attempts = 0
        val observation = StorageObservation()
        val errors = mutableListOf<Throwable>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            observation.observe(flow<Int> { attempts++; throw IllegalArgumentException() }) {
                errors += it; -1
            }.collect {}
        }
        advanceTimeBy(10000); runCurrent()
        assertEquals(1, attempts)
        observation.reload(); runCurrent(); assertEquals(2, attempts)
        var stopped = false
        val canceled = launch {
            StorageObservation().observe(flow<Int> {
                try { awaitCancellation() } finally { stopped = true }
            }) {
                errors += it; -1
            }.collect {}
        }
        runCurrent()
        canceled.cancel(); canceled.join()
        assertTrue(stopped)
        assertFalse(errors.any { it is CancellationException })
    }
}
