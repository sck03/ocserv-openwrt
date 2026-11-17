package dev.opentunnel.vpn.core

import android.app.AppOpsManager
import android.app.usage.NetworkStats
import android.app.usage.NetworkStatsManager
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.TrafficStats
import android.net.Uri
import android.os.Build
import android.os.Process
import android.os.SystemClock
import android.provider.Settings

fun hasUsageStatsPermission(context: Context): Boolean {
    val appOps = context.getSystemService(Context.APP_OPS_SERVICE) as? AppOpsManager ?: return false
    val mode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        appOps.unsafeCheckOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS, Process.myUid(), context.packageName)
    } else {
        @Suppress("DEPRECATION")
        appOps.checkOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS, Process.myUid(), context.packageName)
    }
    return mode == AppOpsManager.MODE_ALLOWED
}

fun openUsageAccessSettings(context: Context) {
    val intent = Intent(Settings.ACTION_USAGE_ACCESS_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    runCatching { context.startActivity(Intent(intent).setData(Uri.parse("package:${context.packageName}"))) }
        .recoverCatching { context.startActivity(intent) }
}

/** Android queries are confined to this adapter; the collector chooses the IO dispatcher. */
class AndroidTrafficStatsProvider(private val context: Context) : TrafficStatsProvider {
    override fun hasPermission() = hasUsageStatsPermission(context)
    override fun getElapsedRealtime() = SystemClock.elapsedRealtime()
    override fun getUidRxBytes(uid: Int) = TrafficStats.getUidRxBytes(uid).coerceAtLeast(0L)
    override fun getUidTxBytes(uid: Int) = TrafficStats.getUidTxBytes(uid).coerceAtLeast(0L)

    override fun getAllUidStats(): Map<Int, UidTrafficBytes> {
        val results = mutableMapOf<Int, UidTrafficBytes>()
        val manager = context.getSystemService(Context.NETWORK_STATS_SERVICE) as? NetworkStatsManager
        if (hasPermission() && manager != null) {
            val now = System.currentTimeMillis()
            for (type in listOf(ConnectivityManager.TYPE_WIFI, ConnectivityManager.TYPE_MOBILE, ConnectivityManager.TYPE_ETHERNET)) {
                runCatching {
                    val stats = manager.querySummary(type, null, 0L, now)
                    try {
                        val bucket = NetworkStats.Bucket()
                        while (stats.hasNextBucket()) {
                            stats.getNextBucket(bucket)
                            if (bucket.uid <= 0) continue
                            val previous = results[bucket.uid] ?: UidTrafficBytes(0, 0)
                            results[bucket.uid] = UidTrafficBytes(previous.rxBytes + bucket.rxBytes, previous.txBytes + bucket.txBytes)
                        }
                    } finally { stats.close() }
                }
            }
        }
        val uid = Process.myUid()
        val current = results[uid] ?: UidTrafficBytes(0, 0)
        results[uid] = UidTrafficBytes(maxOf(current.rxBytes, getUidRxBytes(uid)), maxOf(current.txBytes, getUidTxBytes(uid)))
        return results
    }
}
