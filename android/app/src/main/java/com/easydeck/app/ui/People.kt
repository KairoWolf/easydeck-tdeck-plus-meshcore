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
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import com.easydeck.app.Geo
import com.easydeck.app.MeshRepo
import kotlin.math.cos
import kotlin.math.sin

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
    var mapView by remember { mutableStateOf(false) }
    val self by repo.self.collectAsState()
    Column(Modifier.fillMaxSize().background(C.bg)) {
        Header("People", "${contacts.size} contacts", action = {
            Text("I'M OK", style = tsx(14, C.ok, true), modifier = Modifier.clickable {
                val n = repo.checkIn()
                toast = if (n == 0) "Star some favourites on the T-Deck first" else "Check-in sent to $n favourite${if (n == 1) "" else "s"}"
            }.padding(8.dp))
        })
        toast?.let { Text(it, style = tsx(14, Color.Black, true), modifier = Modifier.fillMaxWidth().background(C.amber).padding(10.dp)) }
        Row(Modifier.fillMaxWidth().background(C.surface)) {
            listOf("LIST", "MAP").forEachIndexed { i, label ->
                Box(Modifier.weight(1f).clickable { mapView = i == 1 }.padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
                    Text(label, style = tsx(13, if (mapView == (i == 1)) C.amber else C.muted, true))
                }
            }
        }
        if (mapView) {
            LaunchedEffect(Unit) { repo.refreshSelf() }   // T-Deck may have a GPS fix by now
            val pts = list.mapNotNull { c -> pos[c.key6]?.let { Triple(c.name, it.lat, it.lon) } }
            Radar(pts, self?.lat ?: 0.0, self?.lon ?: 0.0) { name, la, lo -> openMap(ctx, name, la, lo) { toast = it } }
            return@Column
        }
        LazyColumn {
            items(list, key = { it.key6 }) { c ->
                val p = pos[c.key6]
                Row(Modifier.fillMaxWidth().clickable {
                    if (p != null) openMap(ctx, c.name, p.lat, p.lon) { toast = it }
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

/** Offline "people map": everyone with a known position around the T-Deck (or their centre),
 *  north up, with distance rings. No map tiles, no internet. Tap someone to open a map app. */
@Composable
fun Radar(pts: List<Triple<String, Double, Double>>, selfLat: Double, selfLon: Double, onTap: (String, Double, Double) -> Unit) {
    if (pts.isEmpty()) {
        Box(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
            Text("Nobody has shared a position yet.\n\nPeople appear here when their advert includes a location, or when they share it or send an SOS.",
                style = tsx(15, C.muted))
        }
        return
    }
    val haveSelf = selfLat != 0.0 || selfLon != 0.0
    val cLat = if (haveSelf) selfLat else pts.map { it.second }.average()
    val cLon = if (haveSelf) selfLon else pts.map { it.third }.average()
    val far = pts.maxOf { Geo.distM(cLat, cLon, it.second, it.third) }.coerceAtLeast(50.0)
    val step = Geo.ringStep(far)
    val placed = remember(pts, cLat, cLon) { mutableListOf<Triple<Offset, String, Pair<Double, Double>>>() }
    Column(Modifier.fillMaxSize()) {
        Text(if (haveSelf) "Centre: your T-Deck  ·  rings every ${Geo.fmt(step)}" else "Centre: middle of the group  ·  rings every ${Geo.fmt(step)}",
            style = tsx(13, C.muted), modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp))
        Canvas(Modifier.fillMaxSize().padding(8.dp).pointerInput(pts, cLat, cLon) {
            detectTapGestures { tap ->
                placed.minByOrNull { (it.first - tap).getDistance() }?.let { if ((it.first - tap).getDistance() < 60f) onTap(it.second, it.third.first, it.third.second) }
            }
        }) {
            val cx = size.width / 2; val cy = size.height / 2
            val r = minOf(cx, cy) - 8f
            val ring = Color(0xFF3A4450)
            for (k in 1..3) drawCircle(ring, radius = r * k / 3f, center = Offset(cx, cy), style = Stroke(2f))
            val paint = android.graphics.Paint().apply { color = 0xFF8A96A3.toInt(); textSize = 30f; isAntiAlias = true }
            for (k in 1..3) drawContext.canvas.nativeCanvas.drawText(Geo.fmt(step * k), cx + 6f, cy - r * k / 3f - 6f, paint)
            paint.color = 0xFFFFB000.toInt(); paint.textSize = 34f; paint.isFakeBoldText = true
            drawContext.canvas.nativeCanvas.drawText("N", cx - 10f, cy - r - 14f + 40f, paint)
            drawCircle(if (haveSelf) Color(0xFF2F80ED) else ring, radius = 9f, center = Offset(cx, cy))
            placed.clear()
            val label = android.graphics.Paint().apply { color = 0xFFFFFFFF.toInt(); textSize = 32f; isAntiAlias = true }
            for ((name, la, lo) in pts) {
                val d = Geo.distM(cLat, cLon, la, lo)
                val b = Math.toRadians(Geo.bearing(cLat, cLon, la, lo))
                val rr = (d / (step * 3)).coerceAtMost(1.0) * r
                val p = Offset(cx + (rr * sin(b)).toFloat(), cy - (rr * cos(b)).toFloat())
                drawCircle(Color(0xFFFFB000), radius = 12f, center = p)
                drawCircle(Color.Black, radius = 12f, center = p, style = Stroke(3f))
                drawContext.canvas.nativeCanvas.drawText(name, p.x + 18f, p.y + 10f, label)
                placed.add(Triple(p, name, la to lo))
            }
        }
    }
}

/** Open a position in whatever maps app is installed; no crash if there is none. */
fun openMap(ctx: android.content.Context, name: String, lat: Double, lon: Double, onError: (String) -> Unit) {
    try {
        ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("geo:$lat,$lon?q=$lat,$lon(${Uri.encode(name)})")))
    } catch (e: android.content.ActivityNotFoundException) {
        onError("No maps app installed - position: %.5f, %.5f".format(lat, lon))
    }
}
