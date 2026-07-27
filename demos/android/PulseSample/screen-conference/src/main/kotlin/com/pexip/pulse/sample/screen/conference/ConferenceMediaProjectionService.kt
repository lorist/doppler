package com.pexip.pulse.sample.screen.conference

import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import dagger.hilt.android.AndroidEntryPoint
import javax.inject.Inject

@AndroidEntryPoint
class ConferenceMediaProjectionService : Service() {

    @Inject
    lateinit var manager: NotificationManagerCompat

    private var isForeground = false

    override fun onCreate() {
        super.onCreate()

        val notificationChannel = NotificationChannelCompat
            .Builder(
                ConferenceNotification.CHANNEL_ID,
                NotificationManagerCompat.IMPORTANCE_DEFAULT
            )
            .setName("Conference")
            .setDescription("Displays the ongoing call notifications")
            .build()

        manager.createNotificationChannel(notificationChannel)
    }

    fun startForegroundIfNeeded() {
        if (isForeground) return
        isForeground = true

        val notification = NotificationCompat
            .Builder(this, ConferenceNotification.CHANNEL_ID)
            .setOngoing(true)
            .setContentTitle("Sharing screen")
            .build()

        ServiceCompat.startForeground(
            this,
            NOTIFICATION_ID,
            notification,
            serviceType
        )
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isForeground) {
            ServiceCompat.stopForeground(
                this,
                ServiceCompat.STOP_FOREGROUND_REMOVE
            )
            isForeground = false
        }
    }

    override fun onBind(intent: Intent?): IBinder = MediaProjectionBinder()

    inner class MediaProjectionBinder : Binder() {
        fun getService(): ConferenceMediaProjectionService = this@ConferenceMediaProjectionService
    }

    private val serviceType: Int
        get() = when {
            Build.VERSION.SDK_INT > 29 -> ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            else -> ServiceInfo.FOREGROUND_SERVICE_TYPE_MANIFEST
        }

    private companion object {
        const val NOTIFICATION_ID = 2
    }
}
