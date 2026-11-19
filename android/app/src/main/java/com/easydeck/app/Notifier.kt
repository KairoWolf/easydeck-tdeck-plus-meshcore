package com.easydeck.app

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/** Posts notifications for new incoming messages (and SOS alerts) that arrive after the initial sync. */
class Notifier(private val ctx: Context, private val repo: MeshRepo, private val scope: CoroutineScope) {
    private var seenMax = Long.MIN_VALUE
    var visibleConv: String? = null   // set by the UI: don't notify for the open chat

    fun start() {
        scope.launch {
            repo.messages.collect { msgs ->
                if (repo.syncing.value) { seenMax = msgs.keys.maxOrNull() ?: seenMax; return@collect }
                val fresh = msgs.values.filter { it.seq > seenMax && !it.outgoing && !it.read && !it.silent }
                msgs.keys.maxOrNull()?.let { if (it > seenMax) seenMax = it }
                for (m in fresh) if (m.convId != visibleConv || m.sos) post(m)
            }
        }
        scope.launch {   // keep the foreground service in step with the link
            repo.ble.state.collect { st ->
                try {   // Android may refuse a background FGS start; the link still works while the app is open
                    if (st == BleLink.State.READY) LinkService.start(ctx)
                    else if (st == BleLink.State.IDLE || st == BleLink.State.DISCONNECTED) LinkService.stop(ctx)
                } catch (e: Exception) { }
            }
        }
    }

    private fun post(m: Message) {
        if (ctx.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED) return
        val nm = ctx.getSystemService(NotificationManager::class.java)
        val open = PendingIntent.getActivity(ctx, m.convId.hashCode(),
            Intent(ctx, MainActivity::class.java).putExtra("conv", m.convId).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val title = if (m.sos) "SOS from ${m.name.ifEmpty { repo.nameFor(m.convId) }}" else
            if (m.kind == Message.KIND_CHANNEL) "${m.name} in ${repo.nameFor(m.convId)}" else repo.nameFor(m.convId)
        val n = Notification.Builder(ctx, if (m.sos) EasyDeckApp.CH_SOS else EasyDeckApp.CH_MSG)
            .setSmallIcon(if (m.sos) android.R.drawable.stat_sys_warning else android.R.drawable.sym_action_chat)
            .setContentTitle(title)
            .setContentText(m.text)
            .setStyle(Notification.BigTextStyle().bigText(m.text))
            .setContentIntent(open)
            .setAutoCancel(true)
            .setCategory(if (m.sos) Notification.CATEGORY_ALARM else Notification.CATEGORY_MESSAGE)
            .build()
        nm.notify(m.convId.hashCode(), n)
    }
}
