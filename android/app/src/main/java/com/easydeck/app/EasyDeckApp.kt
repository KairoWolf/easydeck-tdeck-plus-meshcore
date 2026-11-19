package com.easydeck.app

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.Dispatchers

/** Process-wide owner of the mesh connection, so it survives the UI going to the background. */
class EasyDeckApp : Application() {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    lateinit var repo: MeshRepo
        private set
    lateinit var notifier: Notifier
        private set

    override fun onCreate() {
        super.onCreate()
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH_LINK, "Connection", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(CH_MSG, "Messages", NotificationManager.IMPORTANCE_HIGH))
        nm.createNotificationChannel(NotificationChannel(CH_SOS, "SOS alerts", NotificationManager.IMPORTANCE_HIGH).apply {
            description = "Someone on the mesh sent an SOS"
            enableVibration(true)
            vibrationPattern = longArrayOf(0, 800, 300, 800, 300, 800)
        })
        repo = MeshRepo(this, scope)
        notifier = Notifier(this, repo, scope).also { it.start() }
    }

    companion object {
        const val CH_LINK = "link"
        const val CH_MSG = "messages"
        const val CH_SOS = "sos"
    }
}
