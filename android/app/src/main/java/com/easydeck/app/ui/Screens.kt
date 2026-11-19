package com.easydeck.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.easydeck.app.BleLink
import com.easydeck.app.MeshRepo
import com.easydeck.app.Message
import com.easydeck.app.R
import java.text.SimpleDateFormat
import java.util.*

// Tactical palette, matching the T-Deck's default style
object C {
    val bg = Color(0xFF000000); val surface = Color(0xFF0E0E0E); val surface2 = Color(0xFF1A1A1A)
    val border = Color(0xFF3A3A3A); val text = Color(0xFFF2F2F2); val muted = Color(0xFF9C9C9C)
    val faint = Color(0xFF5E5E5E); val amber = Color(0xFFFFB000); val danger = Color(0xFFFF453A)
    val ok = Color(0xFF30D158)
}
val Barlow = FontFamily(Font(R.font.barlow_medium, FontWeight.Medium), Font(R.font.barlow_semibold, FontWeight.SemiBold))
fun tsx(size: Int, color: Color = C.text, bold: Boolean = false) = ts(size, color, bold)
private fun ts(size: Int, color: Color = C.text, bold: Boolean = false) =
    TextStyle(fontFamily = Barlow, fontSize = size.sp, color = color, fontWeight = if (bold) FontWeight.SemiBold else FontWeight.Medium)

@Composable
fun Header(title: String, sub: String? = null, onBack: (() -> Unit)? = null, action: (@Composable () -> Unit)? = null) {
    Column {
        Row(Modifier.fillMaxWidth().background(C.surface).padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically) {
            if (onBack != null) Text("<", style = ts(22, C.amber, true), modifier = Modifier.clickable { onBack() }.padding(end = 12.dp))
            Column(Modifier.weight(1f)) {
                Text(title.uppercase(), style = ts(20, C.text, true), maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (sub != null) Text(sub, style = ts(13, C.muted), maxLines = 1)
            }
            action?.invoke()
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(C.amber))
    }
}

@Composable
fun Tag(text: String, color: Color) {
    Box(Modifier.size(34.dp).background(C.surface2).border(1.dp, C.border), contentAlignment = Alignment.Center) {
        Text(text, style = ts(14, color, true))
    }
}

private val palette = listOf(0xFF3B82F6, 0xFFF97316, 0xFF06B6D4, 0xFFA855F7, 0xFFEAB308, 0xFF22C55E, 0xFFEC4899, 0xFF6366F1)
fun tagColor(key: String) = Color(palette[(key.hashCode() and 0x7fffffff) % palette.size])
fun initials(name: String) = name.split(' ', '-', '_').filter { it.isNotEmpty() }.take(2).joinToString("") { it.first().uppercase() }.ifEmpty { "?" }

// ---------------------------------------------------------------- connect

@Composable
fun ConnectScreen(repo: MeshRepo) {
    val state by repo.ble.state.collectAsState()
    val found by repo.ble.found.collectAsState()
    Column(Modifier.fillMaxSize().background(C.bg)) {
        Header("EasyDeck", "Connect to your T-Deck")
        Column(Modifier.padding(16.dp)) {
            Text("Turn on your T-Deck. When Android asks for a PIN, enter the one shown on the T-Deck under Settings > Connect a phone (default 123456).",
                style = ts(15, C.muted))
            Spacer(Modifier.height(16.dp))
            val scanning = state == BleLink.State.SCANNING
            Box(Modifier.fillMaxWidth().background(C.amber).clickable { if (scanning) repo.ble.stopScan() else repo.ble.startScan() }.padding(14.dp),
                contentAlignment = Alignment.Center) {
                Text(if (scanning) "STOP SCAN" else "SCAN FOR DEVICES", style = ts(16, Color.Black, true))
            }
            Spacer(Modifier.height(12.dp))
            if (state == BleLink.State.CONNECTING) Text("Connecting to ${repo.ble.deviceName}...", style = ts(15, C.amber))
            if (state == BleLink.State.DISCONNECTED) Text("Disconnected", style = ts(15, C.danger))
        }
        LazyColumn {
            items(found, key = { it.address }) { d ->
                Row(Modifier.fillMaxWidth().clickable { repo.connect(d.address, d.name) }.padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Tag(initials(d.name.removePrefix("EasyDeck-").removePrefix("MeshCore-")), C.amber)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text(d.name, style = ts(17, C.text, true))
                        Text(d.address, style = ts(13, C.faint))
                    }
                    Text("${d.rssi} dBm", style = ts(14, C.amber))
                }
            }
        }
    }
}

// ---------------------------------------------------------------- chats

data class Conv(val id: String, val name: String, val last: Message?, val unread: Int, val group: Boolean)

@Composable
fun ChatsScreen(repo: MeshRepo, onOpen: (String) -> Unit) {
    val msgs by repo.messages.collectAsState()
    val contacts by repo.contacts.collectAsState()
    val channels by repo.channels.collectAsState()
    val self by repo.self.collectAsState()
    val syncing by repo.syncing.collectAsState()
    val batt by repo.batteryMv.collectAsState()
    val eds by repo.edsSupported.collectAsState()

    val convs = remember(msgs, contacts, channels) {
        val byConv = msgs.values.filter { !it.silent }.groupBy { it.convId }
        val list = byConv.map { (id, ms) ->
            val last = ms.maxByOrNull { it.seq }
            Conv(id, repo.nameFor(id), last, ms.count { !it.outgoing && !it.read }, id.startsWith("1:"))
        }.sortedByDescending { it.last?.storedAt ?: 0 }.toMutableList()
        for (ch in channels) {
            val id = "1:" + ch.secret.copyOf(6).joinToString("") { "%02x".format(it) }
            if (list.none { it.id == id }) list.add(Conv(id, ch.name, null, 0, true))
        }
        list
    }
    Column(Modifier.fillMaxSize().background(C.bg)) {
        val sub = buildString {
            append(self?.name ?: repo.ble.deviceName)
            if (batt > 0) append("  ·  %.2fV".format(batt / 1000f))
            if (syncing) append("  ·  syncing...")
            if (eds == false) append("  ·  stock firmware")
        }
        val linkState by repo.ble.state.collectAsState()
        val online = linkState == BleLink.State.READY
        Header("Chats", if (online) sub else "Offline - showing saved history", action = {
            if (online) Text("ADVERT", style = ts(14, C.amber, true), modifier = Modifier.clickable { repo.advert() }.padding(8.dp))
            else Text("CONNECT", style = ts(14, C.amber, true), modifier = Modifier.clickable { repo.ble.startScan() }.padding(8.dp))
            Text("X", style = ts(16, C.muted, true), modifier = Modifier.clickable { repo.disconnect(forget = true) }.padding(8.dp))
        })
        val sos = remember(msgs) { repo.unreadSos() }
        sos.lastOrNull()?.let { m ->
            Column(Modifier.fillMaxWidth().background(C.danger).clickable { onOpen(m.convId) }.padding(12.dp)) {
                Text("SOS from ${m.name.ifEmpty { repo.nameFor(m.convId) }}", style = ts(18, Color.White, true))
                Text(m.text, style = ts(14, Color.White), maxLines = 2)
                Text("Tap to open the chat and reply", style = ts(12, Color(0xCCFFFFFF)))
            }
        }
        if (!online && linkState == BleLink.State.SCANNING) {
            val found by repo.ble.found.collectAsState()
            found.forEach { d ->
                Text("Connect to ${d.name}", style = ts(16, C.amber, true),
                    modifier = Modifier.fillMaxWidth().clickable { repo.connect(d.address, d.name) }.padding(12.dp))
            }
        }
        LazyColumn {
            items(convs, key = { it.id }) { c ->
                Row(Modifier.fillMaxWidth().clickable { onOpen(c.id) }.padding(horizontal = 12.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Tag(if (c.group) "#" else initials(c.name), if (c.group) C.amber else tagColor(c.id))
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Row {
                            Text(c.name, style = ts(17, C.text, true), modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            c.last?.let { Text(fmtTime(it.storedAt), style = ts(13, if (c.unread > 0) C.amber else C.faint)) }
                        }
                        val pv = c.last?.let { (if (it.outgoing) "You: " else if (c.group && it.name.isNotEmpty()) "${it.name}: " else "") + it.text } ?: "No messages yet"
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(pv, style = ts(14, if (c.unread > 0) C.text else C.muted), maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                            if (c.unread > 0) Box(Modifier.padding(start = 6.dp).background(C.amber).padding(horizontal = 6.dp)) {
                                Text("${c.unread}", style = ts(13, Color.Black, true))
                            }
                        }
                    }
                }
                Box(Modifier.fillMaxWidth().padding(start = 58.dp).height(1.dp).background(C.surface2))
            }
        }
    }
}

fun fmtTime(t: Long): String {
    if (t < 1700000000L) return ""
    val d = Date(t * 1000)
    val now = Calendar.getInstance()
    val c = Calendar.getInstance().apply { time = d }
    return if (now.get(Calendar.YEAR) == c.get(Calendar.YEAR) && now.get(Calendar.DAY_OF_YEAR) == c.get(Calendar.DAY_OF_YEAR))
        SimpleDateFormat("HH:mm", Locale.getDefault()).format(d)
    else SimpleDateFormat("d MMM", Locale.getDefault()).format(d)
}

// ---------------------------------------------------------------- chat

@Composable
fun ChatScreen(repo: MeshRepo, convId: String, onBack: () -> Unit) {
    val msgs by repo.messages.collectAsState()
    val list = remember(msgs, convId) { msgs.values.filter { it.convId == convId && !it.silent }.sortedBy { it.seq } }
    val group = convId.startsWith("1:")
    var input by remember { mutableStateOf("") }
    val listState = rememberLazyListState()
    LaunchedEffect(list.size) {
        if (list.isNotEmpty()) listState.animateScrollToItem(list.size - 1)
        repo.markRead(convId)
    }
    Column(Modifier.fillMaxSize().background(C.bg).imePadding()) {
        Header(repo.nameFor(convId), if (group) "Group" else "Direct message", onBack = onBack)
        LazyColumn(Modifier.weight(1f).padding(horizontal = 10.dp), state = listState) {
            items(list, key = { it.seq }) { m -> Bubble(m, group) }
        }
        Row(Modifier.fillMaxWidth().background(C.surface).padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).border(1.dp, C.border).padding(horizontal = 10.dp, vertical = 10.dp)) {
                if (input.isEmpty()) Text("Message", style = ts(16, C.faint))
                BasicTextField(input, { if (it.toByteArray().size <= 140) input = it }, textStyle = ts(16), cursorBrush = SolidColor(C.amber),
                    modifier = Modifier.fillMaxWidth())
            }
            Spacer(Modifier.width(8.dp))
            Box(Modifier.background(if (input.isBlank()) C.surface2 else C.amber).clickable(enabled = input.isNotBlank()) {
                repo.send(convId, input.trim()); input = ""
            }.padding(horizontal = 16.dp, vertical = 12.dp)) {
                Text("SEND", style = ts(15, if (input.isBlank()) C.faint else Color.Black, true))
            }
        }
    }
}

@Composable
fun Bubble(m: Message, group: Boolean) {
    val mine = m.outgoing
    Column(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalAlignment = if (mine) Alignment.End else Alignment.Start) {
        Column(Modifier.widthIn(max = 300.dp)
            .background(if (m.sos) C.danger else if (mine) C.amber else C.surface2)
            .padding(horizontal = 10.dp, vertical = 6.dp)) {
            if (!mine && group && m.name.isNotEmpty()) Text(m.name, style = ts(13, tagColor(m.name), true))
            Text(m.text, style = ts(16, if (mine) Color.Black else C.text))
            val st = if (!mine) "" else when (m.status) {
                Message.ST_DELIVERED -> "  ✓✓"; Message.ST_FAILED -> "  not delivered"; Message.ST_PENDING -> "  sending"; else -> "  ✓"
            }
            val via = when (m.origin) { 1 -> "  · T-Deck"; 2 -> "  · phone"; else -> "" }
            Text(fmtTime(if (mine) m.senderTs else m.storedAt) + st + via, style = ts(12, if (mine) Color(0xAA000000) else C.muted))
        }
    }
}
