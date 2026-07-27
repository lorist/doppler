package com.pexip.pulse.sample.screen.conference

import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.IBinder
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import dagger.hilt.android.AndroidEntryPoint
import javax.inject.Inject

@AndroidEntryPoint
class ConferenceService : Service() {

    @Inject
    lateinit var manager: NotificationManagerCompat

    override fun onCreate() {
        super.onCreate()

        val notificationChannel = NotificationChannelCompat
            .Builder(
                ConferenceNotification.CHANNEL_ID,
                NotificationManagerCompat.IMPORTANCE_DEFAULT
            )
            .setName("Conference")
            .build()

        manager.createNotificationChannel(notificationChannel)

        val notification = NotificationCompat
            .Builder(this, notificationChannel.id)
            .setOngoing(true)
            .setContentTitle("Conference")
            .build()

        ServiceCompat.startForeground(
            this,
            NOTIFICATION_ID,
            notification,
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MANIFEST
        )
    }

    override fun onDestroy() {
        super.onDestroy()
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
    }

    override fun onBind(intent: Intent?): IBinder = Binder()

    private companion object {
        const val NOTIFICATION_ID = 1
    }
}

object ConferenceNotification {
    const val CHANNEL_ID = "conference"
}
