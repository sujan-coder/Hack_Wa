package com.hackwa.companion.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat

/**
 * Foreground service that keeps BLE scanning / connection alive
 * even when the app is in the background.
 *
 * Uses LOW_POWER scan + auto-reconnect from BleManager.
 * Battery impact is minimal (BLE scanning at low duty cycle).
 */
class BleConnectionService : Service() {

    companion object {
        private const val TAG = "BleService"
        private const val CHANNEL_ID = "hackwa_ble_channel"
        private const val NOTIFICATION_ID = 1001

        // Singleton reference so Activity/ViewModel can share the same BleManager
        @Volatile
        var bleManager: BleManager? = null
    }

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "Service onCreate")
        if (bleManager == null) {
            bleManager = BleManager(applicationContext)
        }
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Searching for HackWa watch..."))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(TAG, "Service onStartCommand")
        val mgr = bleManager ?: return START_STICKY

        // If already connected, just update notification
        if (mgr.isConnected()) {
            updateNotification("Connected to ${mgr.getSavedName() ?: "HackWa"}")
            return START_STICKY
        }

        // Start auto-connect if we have a saved device
        if (mgr.hasSavedDevice()) {
            mgr.startAutoConnect()
            updateNotification("Searching for ${mgr.getSavedName() ?: "HackWa"}...")
        } else {
            updateNotification("Open app to pair your watch")
        }

        return START_STICKY  // restart if killed
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        Log.d(TAG, "Service onDestroy")
        super.onDestroy()
    }

    // ── Notifications ──────────────────────────────────────
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "HackWa Connection",
                NotificationManager.IMPORTANCE_LOW   // no sound, low battery
            ).apply {
                description = "Keeps BLE connection to your HackWa watch alive"
                setShowBadge(false)
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(text: String): Notification {
        // Tapping notification opens the app
        val launchIntent = packageManager.getLaunchIntentForPackage(packageName)
        val pi = PendingIntent.getActivity(
            this, 0, launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("HackWa")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .setContentIntent(pi)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    fun updateNotification(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIFICATION_ID, buildNotification(text))
    }
}
