package dev.opentunnel.vpn.data

import android.content.Context
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.util.LruCache
import android.os.SystemClock
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.text.Collator

@Immutable
data class InstalledApp(
    val packageName: String,
    val label: String,
    val isSystem: Boolean,
    val uid: Int = 0,
)

/**
 * Lists installed applications on the device.
 * Excludes internal OS framework components and overlays so only real, user-facing
 * applications are listed and monitored.
 */
object InstalledApps {

    private const val ICON_PX = 96
    private const val ICON_BUDGET = 4 * 1024 * 1024
    private val iconCache = object : LruCache<String, ImageBitmap>(ICON_BUDGET) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }
    private var callbacksRegistered = false
    @Synchronized private fun registerMemoryCallbacks(context: Context) {
        if (callbacksRegistered) return
        context.applicationContext.registerComponentCallbacks(object : ComponentCallbacks2 {
            override fun onConfigurationChanged(config: Configuration) { iconCache.evictAll() }
            override fun onLowMemory() { iconCache.evictAll() }
            override fun onTrimMemory(level: Int) {
                if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW) iconCache.evictAll()
            }
        })
        callbacksRegistered = true
    }

    private var cachedList: List<InstalledApp>? = null
    private val loadMutex = Mutex()
    private var lastLoadTime = 0L
    private const val CACHE_TTL_MS = 5 * 60 * 1000L

    suspend fun load(context: Context): List<InstalledApp> = withContext(Dispatchers.IO) {
        loadMutex.withLock {
        val now = SystemClock.elapsedRealtime()
        if (cachedList != null && (now - lastLoadTime < CACHE_TTL_MS)) {
            return@withLock cachedList!!
        }

        val pm = context.packageManager
        val self = context.packageName
        val collator = Collator.getInstance()

        val packages = runCatching {
            pm.getInstalledApplications(0)
        }.recoverCatching {
            pm.getInstalledPackages(0).mapNotNull { it.applicationInfo }
        }.getOrElse { emptyList() }

        val result = packages.asSequence()
            .filter { it.packageName != self && it.uid > 0 }
            .filter { info ->
                val isPureSystem = (info.flags and ApplicationInfo.FLAG_SYSTEM) != 0 &&
                    (info.flags and ApplicationInfo.FLAG_UPDATED_SYSTEM_APP) == 0

                // 1. User-installed apps: always included
                // 2. Updated system apps (e.g. Chrome, YouTube from Play Store): included
                // 3. Pre-installed system apps: only include if they have a launchable UI / icon
                if (!isPureSystem) {
                    true
                } else {
                    pm.getLaunchIntentForPackage(info.packageName) != null
                }
            }
            .map { info ->
                val isSystem = (info.flags and ApplicationInfo.FLAG_SYSTEM) != 0
                val label = runCatching { pm.getApplicationLabel(info).toString() }
                    .getOrDefault(info.packageName)

                InstalledApp(
                    packageName = info.packageName,
                    label = if (label.isNotBlank()) label else info.packageName,
                    isSystem = isSystem,
                    uid = info.uid,
                )
            }
            .sortedWith(compareBy(collator) { it.label })
            .toList()

        cachedList = result
        lastLoadTime = now
        result
        }
    }

    suspend fun icon(context: Context, packageName: String): ImageBitmap? {
        registerMemoryCallbacks(context)
        iconCache.get(packageName)?.let { return it }
        return withContext(Dispatchers.IO) {
            runCatching {
                val drawable = context.packageManager.getApplicationIcon(packageName)
                val bitmap = drawable.toBitmap(ICON_PX, ICON_PX, Bitmap.Config.ARGB_8888)
                bitmap.asImageBitmap().also { iconCache.put(packageName, it) }
            }.getOrNull()
        }
    }

}
