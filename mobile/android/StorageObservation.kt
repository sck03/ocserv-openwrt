package dev.opentunnel.vpn.data

import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.retryWhen
import kotlinx.coroutines.flow.update

/** One retry policy for long-lived storage observers, independent of Android UI. */
class StorageObservation(private val recoverable: (Throwable) -> Boolean = { it is IOException }) {
    private val revision = MutableStateFlow(0L)
    fun reload() { revision.update { it + 1 } }

    @OptIn(ExperimentalCoroutinesApi::class)
    fun <T> observe(source: Flow<T>, unavailable: (Throwable) -> T): Flow<T> = revision.flatMapLatest {
        flow {
            var failures = 0
            emitAll(source.onEach { failures = 0 }.retryWhen { error, _ ->
                if (error is CancellationException) throw error
                if (!recoverable(error) || failures >= 3) return@retryWhen false
                emit(unavailable(error))
                delay(250L shl failures++)
                true
            }.catch { error ->
                if (error is CancellationException) throw error
                emit(unavailable(error))
            })
        }
    }
}
