package dev.opentunnel.vpn.core

data class SpeedSnapshot(val session: Long = 0, val rx: List<Long> = emptyList(), val tx: List<Long> = emptyList())

/** Bounded one-second buckets; a five-second screen-off sample spans five buckets. */
internal class SpeedHistory(private val capacity: Int = 18000) {
    private val rx = LongArray(capacity)
    private val tx = LongArray(capacity)
    private var head = 0
    private var count = 0
    private var second = -1L
    private var epoch = 0L
    init { require(capacity > 0) }

    @Synchronized fun clear(session: Long = epoch) { head = 0; count = 0; second = -1; epoch = maxOf(epoch, session) }

    @Synchronized fun sample(elapsed: Long, received: Long, sent: Long, session: Long = 0) {
        if (session < epoch || elapsed < session) return
        if (session != epoch) clear(session)
        val now = elapsed / 1000
        if (second > now) clear()
        if (second == now && count > 0) {
            val last = (head + count - 1) % capacity
            rx[last] = received; tx[last] = sent
            return
        }
        val steps = if (second < 0) 1 else (now - second).coerceAtMost(capacity.toLong()).toInt()
        repeat(steps) {
            val index = (head + count) % capacity
            rx[index] = received; tx[index] = sent
            if (count < capacity) count++ else head = (head + 1) % capacity
        }
        second = now
    }

    @Synchronized fun snapshot(): SpeedSnapshot {
        fun copy(source: LongArray): List<Long> {
            val values = LongArray(count) { source[(head + it) % capacity] }
            return object : AbstractList<Long>() {
                override val size get() = values.size
                override fun get(index: Int): Long = values[index]
            }
        }
        return SpeedSnapshot(epoch, copy(rx), copy(tx))
    }
}
