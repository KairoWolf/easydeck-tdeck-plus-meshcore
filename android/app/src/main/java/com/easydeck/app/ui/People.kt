package com.easydeck.app.ui

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.easydeck.app.MeshRepo

@Composable
fun PeopleScreen(repo: MeshRepo, onChat: (String) -> Unit) {
    val contacts by repo.contacts.collectAsState()
    val msgs by repo.messages.collectAsState()
    val ctx = LocalContext.current
    val pos = remember(contacts, msgs) { repo.positions() }
    val list = remember(contacts) {
        contacts.values.sortedWith(compareByDescending<com.easydeck.app.Contact> { it.favorite }
            .thenBy { if (it.type == 1) 0 else 1 }.thenByDescending { it.lastMod })
    }
    var toast by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxSize().background(C.bg)) {
        Header("People", "${contacts.size} contacts", action = {
            Text("I'M OK", style = tsx(14, C.ok, true), modifier = Modifier.clickable {
                val n = repo.checkIn()
                toast = if (n == 0) "Star some favourites on the T-Deck first" else "Check-in sent to $n favourite${if (n == 1) "" else "s"}"
            }.padding(8.dp))
        })
        toast?.let { Text(it, style = tsx(14, Color.Black, true), modifier = Modifier.fillMaxWidth().background(C.amber).padding(10.dp)) }
        LazyColumn {
            items(list, key = { it.key6 }) { c ->
                val p = pos[c.key6]
                Row(Modifier.fillMaxWidth().clickable {
                    if (p != null) ctx.startActivity(Intent(Intent.ACTION_VIEW,
                        Uri.parse("geo:${p.lat},${p.lon}?q=${p.lat},${p.lon}(${Uri.encode(c.name)})")))
                    else if (c.type == 1) onChat("0:" + c.key6)
                }.padding(horizontal = 12.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Tag(if (c.type == 1) initials(c.name) else if (c.type == 2) "R" else "#", if (c.type == 1) tagColor("0:" + c.key6) else C.amber)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(c.name, style = tsx(17, C.text, true), maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false))
                            if (c.favorite) Text("  ★", style = tsx(15, C.amber, true))
                        }
                        val kind = when (c.type) { 2 -> "Repeater"; 3 -> "Room"; 4 -> "Sensor"; else -> null }
                        val heard = if (c.lastMod > 1700000000L) "heard ${fmtTime(c.lastMod)}" else ""
                        val where = p?.let { "%.4f, %.4f (%s %s)".format(it.lat, it.lon, it.source, fmtTime(it.time)) }
                        Text(listOfNotNull(kind, where ?: heard.ifEmpty { null }).joinToString("  ·  ").ifEmpty { "no position yet" },
                            style = tsx(13, if (p != null) C.amber else C.muted), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    if (p != null) Text("MAP", style = tsx(13, C.amber, true))
                }
                Box(Modifier.fillMaxWidth().padding(start = 58.dp).height(1.dp).background(C.surface2))
            }
        }
    }
}
