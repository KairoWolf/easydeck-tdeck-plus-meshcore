package com.easydeck.app

import android.content.Context
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Talks to the device and keeps the app's view of the world. The T-Deck is the single source of
 * truth for message history: we mirror it with the EasyDeck Sync Protocol, so messages typed on
 * the T-Deck show up here and messages sent from here show up on the T-Deck.
 */
class MeshRepo(private val ctx: Context, private val scope: CoroutineScope) {
    val ble = BleLink(ctx)
    private val prefs = ctx.getSharedPreferences("easydeck", Context.MODE_PRIVATE)
    private val historyFile = File(ctx.filesDir, "history.bin")
    @Volatile private var userDisconnected = false
    private var saveJob: Job? = null

    private val _self = MutableStateFlow<SelfInfo?>(null)
    val self: StateFlow<SelfInfo?> = _self.asStateFlow()
    private val _contacts = MutableStateFlow<Map<String, Contact>>(emptyMap())
    val contacts: StateFlow<Map<String, Contact>> = _contacts.asStateFlow()
    private val _channels = MutableStateFlow<List<Channel>>(emptyList())
    val channels: StateFlow<List<Channel>> = _channels.asStateFlow()
    private val _messages = MutableStateFlow<Map<Long, Message>>(emptyMap())
    val messages: StateFlow<Map<Long, Message>> = _messages.asStateFlow()
    private val _syncing = MutableStateFlow(false)
    val syncing: StateFlow<Boolean> = _syncing.asStateFlow()
    private val _eds = MutableStateFlow<Boolean?>(null)        // null = unknown, false = stock firmware
    val edsSupported: StateFlow<Boolean?> = _eds.asStateFlow()
    private val _battMv = MutableStateFlow(0)
    val batteryMv: StateFlow<Int> = _battMv.asStateFlow()

    private var logId = 0L
    private var legacySeq = -1L                               // stock firmware: synthetic negative seqs
    private var waiter: Pair<(ByteArray) -> Boolean, CompletableDeferred<ByteArray>>? = null
    private var syncJob: Job? = null
    @Volatile private var fetchPending = false

    init {
        loadHistory()
        scope.launch { for (f in ble.frames) onFrame(f) }
        scope.launch {
            ble.state.collect { st ->
                if (st == BleLink.State.READY) {
                    prefs.edit().putString("addr", lastAddr).putString("name", ble.deviceName).apply()
                    reconnectDelay = 5000
                    startSync()
                } else if (st == BleLink.State.DISCONNECTED) {
                    syncJob?.cancel()
                    if (!userDisconnected) scheduleReconnect()
                }
            }
        }
        scope.launch { messages.collect { scheduleSave() } }
        reconnectLast()
    }

    /** Reconnect to the last device (no-op until Bluetooth permission is granted). */
    fun reconnectLast() {
        if (ble.state.value == BleLink.State.READY || ble.state.value == BleLink.State.CONNECTING) return
        prefs.getString("addr", null)?.let { a -> connect(a, prefs.getString("name", "") ?: "") }
    }

    // ---------------------------------------------------------------- connection management
    private var lastAddr: String? = null
    private var reconnectDelay = 5000L

    fun connect(address: String, name: String) {
        userDisconnected = false
        lastAddr = address
        ble.connect(address, name)
    }

    fun disconnect(forget: Boolean = false) {
        userDisconnected = true
        ble.disconnect()
        if (forget) prefs.edit().remove("addr").remove("name").apply()
    }

    private fun scheduleReconnect() {
        val addr = lastAddr ?: return
        scope.launch {
            delay(reconnectDelay)
            reconnectDelay = (reconnectDelay * 2).coerceAtMost(60000)
            if (!userDisconnected && ble.state.value == BleLink.State.DISCONNECTED) ble.connect(addr, ble.deviceName)
        }
    }

    // ---------------------------------------------------------------- local history (offline viewing)
    private fun scheduleSave() {
        saveJob?.cancel()
        saveJob = scope.launch(Dispatchers.IO) { delay(2000); saveHistory() }
    }

    private fun saveHistory() {
        val msgs = _messages.value.values.sortedBy { it.seq }.takeLast(5000)
        val tmp = File(historyFile.path + ".tmp")
        DataOutputStream(tmp.outputStream().buffered()).use { o ->
            o.writeInt(1); o.writeLong(logId)
            o.writeInt(_contacts.value.size)
            for (c in _contacts.value.values) {
                o.write(c.pubkey); o.writeByte(c.type); o.writeByte(c.flags); o.writeUTF(c.name)
                o.writeLong(c.lastAdvert); o.writeDouble(c.lat); o.writeDouble(c.lon); o.writeLong(c.lastMod)
            }
            o.writeInt(_channels.value.size)
            for (ch in _channels.value) { o.writeByte(ch.idx); o.writeUTF(ch.name); o.write(ch.secret) }
            o.writeInt(msgs.size)
            for (m in msgs) {
                o.writeLong(m.seq); o.writeLong(m.storedAt); o.writeLong(m.senderTs); o.writeByte(m.kind); o.writeByte(m.dir)
                o.writeByte(m.status); o.writeByte(m.origin); o.write(m.key); o.writeByte(m.channelIdx); o.writeByte(m.flags)
                o.writeFloat(m.snr); o.writeByte(m.pathLen); o.writeUTF(m.name); o.writeUTF(m.text)
            }
        }
        tmp.renameTo(historyFile)
    }

    private fun loadHistory() {
        if (!historyFile.exists()) return
        try {
            DataInputStream(historyFile.inputStream().buffered()).use { i ->
                if (i.readInt() != 1) return
                logId = i.readLong()
                val cs = HashMap<String, Contact>()
                repeat(i.readInt()) {
                    val pk = ByteArray(32).also { b -> i.readFully(b) }
                    val c = Contact(pk, i.readUnsignedByte(), i.readUnsignedByte(), i.readUTF(), i.readLong(), i.readDouble(), i.readDouble(), i.readLong())
                    cs[c.key6] = c
                }
                val chs = ArrayList<Channel>()
                repeat(i.readInt()) {
                    val idx = i.readUnsignedByte(); val name = i.readUTF()
                    chs.add(Channel(idx, name, ByteArray(16).also { b -> i.readFully(b) }))
                }
                val ms = HashMap<Long, Message>()
                repeat(i.readInt()) {
                    val seq = i.readLong(); val st = i.readLong(); val sts = i.readLong()
                    val kind = i.readUnsignedByte(); val dir = i.readUnsignedByte(); val status = i.readUnsignedByte(); val origin = i.readUnsignedByte()
                    val key = ByteArray(6).also { b -> i.readFully(b) }
                    val chIdx = i.readUnsignedByte(); val flags = i.readUnsignedByte(); val snr = i.readFloat(); val pl = i.readUnsignedByte()
                    ms[seq] = Message(seq, st, sts, kind, dir, status, origin, key, chIdx, flags, snr, pl, i.readUTF(), i.readUTF())
                }
                _contacts.value = cs; _channels.value = chs; _messages.value = ms
                legacySeq = minOf(-1L, (ms.keys.minOrNull() ?: 0L) - 1)
            }
        } catch (e: Exception) { historyFile.delete() }
    }

    // ---------------------------------------------------------------- request helper
    private val reqLock = Mutex()   // one request/response exchange at a time

    private suspend fun request(frame: ByteArray, timeoutMs: Long = 4000, accept: (ByteArray) -> Boolean): ByteArray? =
        reqLock.withLock {
            val d = CompletableDeferred<ByteArray>()
            waiter = accept to d
            ble.send(frame)
            withTimeoutOrNull(timeoutMs) { d.await() }.also { waiter = null }
        }

    private fun code(f: ByteArray) = if (f.isEmpty()) -1 else f[0].toInt() and 0xFF

    /** Re-read the T-Deck's own info (e.g. its GPS position once it has a fix). */
    fun refreshSelf() {
        if (ble.state.value != BleLink.State.READY) return
        scope.launch { request(Proto.appStart()) { code(it) == Proto.RESP_SELF_INFO }?.let { _self.value = Parse.selfInfo(it) } }
    }

    // ---------------------------------------------------------------- sync
    private fun startSync() {
        syncJob?.cancel()
        syncJob = scope.launch {
            _syncing.value = true
            try {
                request(Proto.appStart()) { code(it) == Proto.RESP_SELF_INFO }?.let { _self.value = Parse.selfInfo(it) }
                request(Proto.setTime(System.currentTimeMillis() / 1000)) { code(it) == Proto.RESP_OK || code(it) == Proto.RESP_ERR }
                loadContacts()
                loadChannels()
                request(Proto.getBattery()) { code(it) == Proto.RESP_BATT_AND_STORAGE }?.let {
                    if (it.size >= 3) _battMv.value = (it[1].toInt() and 0xFF) or ((it[2].toInt() and 0xFF) shl 8)
                }
                val hello = request(Proto.edsHello()) { code(it) == Proto.EDS_RESP_HELLO || code(it) == Proto.RESP_ERR }
                if (hello != null && code(hello) == Proto.EDS_RESP_HELLO && hello.size >= 14) {
                    _eds.value = true
                    val id = Proto.le(4).put(hello, 2, 4).getInt(0).toLong() and 0xFFFFFFFFL
                    if (id != logId) { logId = id; _messages.value = _messages.value.filterKeys { it < 0 } }   // different device / log reset
                    fetchSince(-1)
                    // refresh delivery/read status of the most recent messages
                    val last = _messages.value.keys.maxOrNull() ?: -1
                    if (last > 50) fetchSince(last - 50)
                } else {
                    _eds.value = false   // stock MeshCore: fall back to the offline queue
                    drainQueue()
                }
            } finally { _syncing.value = false }
        }
    }

    private suspend fun loadContacts() {
        val map = HashMap<String, Contact>()
        val done = request(Proto.getContacts(), 15000) { f ->
            when (code(f)) {
                Proto.RESP_CONTACT -> { Parse.contact(f)?.let { map[it.key6] = it }; false }
                Proto.RESP_END_OF_CONTACTS -> true
                else -> false
            }
        }
        if (done != null || map.isNotEmpty()) _contacts.value = map
    }

    private suspend fun loadChannels() {
        val list = ArrayList<Channel>()
        for (idx in 0 until 40) {
            val r = request(Proto.getChannel(idx), 2500) { code(it) == Proto.RESP_CHANNEL_INFO || code(it) == Proto.RESP_ERR } ?: break
            if (code(r) == Proto.RESP_ERR) break
            Parse.channel(r)?.let { list.add(it) }
        }
        _channels.value = list
    }

    /** Pull every record after the newest one we have (or after [from]). */
    private suspend fun fetchSince(from: Long) {
        while (true) {
            val start = maxOf(from, _messages.value.keys.maxOrNull() ?: -1) + 1
            var got = 0
            val endFrame = request(Proto.edsGetSince(start, 32), 8000) { f ->
                if (code(f) == Proto.EDS_RESP_RECORD) { Parse.record(f)?.let { put(it) }; got++; false }
                else code(f) == Proto.EDS_RESP_END
            } ?: break
            if (got == 0 || endFrame.size < 5) break
        }
    }

    private suspend fun drainQueue() {
        repeat(256) {
            val f = request(Proto.syncNext(), 3000) { c -> code(c) in listOf(7, 8, 16, 17, Proto.RESP_NO_MORE_MESSAGES) } ?: return
            if (code(f) == Proto.RESP_NO_MORE_MESSAGES) return
            legacyMessage(f)?.let { put(it) }
        }
    }

    /** Stock firmware message frames (v3) -> Message, so the app also works with plain MeshCore. */
    private fun legacyMessage(f: ByteArray): Message? {
        val now = System.currentTimeMillis() / 1000
        return when (code(f)) {
            16 -> if (f.size >= 16) {
                val key = f.copyOfRange(4, 10)
                val name = _contacts.value[key.hex()]?.name ?: key.hex(3)
                Message(legacySeq--, now, Proto.le(4).put(f, 12, 4).getInt(0).toLong(), 0, 0, 0, 0, key, 0, 1, f[1] / 4f, f[10].toInt() and 0xFF, name, String(f, 16, f.size - 16))
            } else null
            17 -> if (f.size >= 11) {
                val ch = _channels.value.firstOrNull { it.idx == (f[4].toInt() and 0xFF) }
                val raw = String(f, 11, f.size - 11)
                val sep = raw.indexOf(": ")
                Message(legacySeq--, now, Proto.le(4).put(f, 7, 4).getInt(0).toLong(), 1, 0, 0, 0, ch?.secret?.copyOf(6) ?: ByteArray(6),
                    f[4].toInt() and 0xFF, 1, f[1] / 4f, f[5].toInt() and 0xFF,
                    if (sep > 0) raw.substring(0, sep) else "", if (sep > 0) raw.substring(sep + 2) else raw)
            } else null
            else -> null
        }
    }

    private fun put(m: Message) { _messages.value = _messages.value + (m.seq to m) }

    // ---------------------------------------------------------------- incoming frames
    private fun onFrame(f: ByteArray) {
        waiter?.let { (accept, d) -> if (accept(f)) { d.complete(f); return } }
        when (code(f)) {
            Proto.EDS_PUSH_NEW -> if (!fetchPending) {
                fetchPending = true
                scope.launch { try { fetchSince(-1) } finally { fetchPending = false } }
            }
            Proto.EDS_PUSH_UPDATE -> Parse.record(f)?.let { put(it) }
            Proto.PUSH_MSG_WAITING -> scope.launch {
                if (_eds.value == false) drainQueue()
                else { // the shared log already has it; just keep the device's queue empty
                    repeat(64) { if (code(request(Proto.syncNext(), 2000) { c -> code(c) in listOf(7, 8, 16, 17, 10) } ?: return@launch) == 10) return@launch }
                }
            }
            Proto.PUSH_NEW_ADVERT, Proto.PUSH_ADVERT -> scope.launch { loadContacts() }
        }
    }

    // ---------------------------------------------------------------- actions
    fun send(convId: String, text: String) {
        val (kindS, keyHex) = convId.split(":")
        val ts = System.currentTimeMillis() / 1000
        if (kindS.toInt() == Message.KIND_CHANNEL) {
            val ch = _channels.value.firstOrNull { it.key6 == keyHex.take(12) } ?: return
            ble.send(Proto.sendChannel(ch.idx, text, ts))
        } else {
            val c = _contacts.value[keyHex.take(12)] ?: return
            ble.send(Proto.sendDirect(c.pubkey, text, ts))
        }
        // EasyDeck records it and pushes it back; stock firmware won't, so show it locally
        if (_eds.value == false) {
            val k = keyHex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
            put(Message(legacySeq--, ts, ts, kindS.toInt(), 1, Message.ST_SENT, 2, k, 0, 1, 0f, 0, _self.value?.name ?: "", text))
        }
    }

    fun markRead(convId: String) {
        if (_eds.value != true) return
        val (kindS, keyHex) = convId.split(":")
        val k = keyHex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
        ble.send(Proto.edsMarkRead(kindS.toInt(), k))
    }

    fun advert() = ble.send(Proto.advert(true))

    /** Last known position per contact (key6 hex): from adverts, or from LOC/SOS/check-in messages. */
    data class Pos(val lat: Double, val lon: Double, val time: Long, val source: String)
    private val locRe = Regex("(-?\\d{1,2}\\.\\d{3,}),\\s?(-?\\d{1,3}\\.\\d{3,})")
    fun positions(): Map<String, Pos> {
        val out = HashMap<String, Pos>()
        for (c in _contacts.value.values) if (c.lat != 0.0 || c.lon != 0.0) out[c.key6] = Pos(c.lat, c.lon, c.lastAdvert, "advert")
        for (m in _messages.value.values.sortedBy { it.seq }) {
            if (m.outgoing || m.kind != Message.KIND_DIRECT) continue
            if (!(m.text.startsWith("LOC ") || m.text.startsWith("SOS!") || m.text.startsWith("OK check-in"))) continue
            val g = locRe.find(m.text) ?: continue
            val key = m.key.hex()
            val prev = out[key]
            if (prev == null || m.storedAt >= prev.time)
                out[key] = Pos(g.groupValues[1].toDouble(), g.groupValues[2].toDouble(), m.storedAt,
                    if (m.text.startsWith("SOS!")) "SOS" else if (m.text.startsWith("LOC ")) "shared" else "check-in")
        }
        return out
    }

    /** "I'm OK" check-in to every favourite, sent from the phone. */
    fun checkIn(): Int {
        val favs = _contacts.value.values.filter { it.favorite && it.type == 1 }
        val text = "OK check-in from ${_self.value?.name ?: "me"} (via phone)."
        for (c in favs) send("0:" + c.pubkey.hex(6), text)
        return favs.size
    }

    fun unreadSos(): List<Message> = _messages.value.values.filter { it.sos && !it.read && !it.outgoing }

    fun nameFor(convId: String): String {
        val (kindS, keyHex) = convId.split(":")
        return if (kindS.toInt() == Message.KIND_CHANNEL)
            _channels.value.firstOrNull { it.key6 == keyHex.take(12) }?.name ?: "Group"
        else _contacts.value[keyHex.take(12)]?.name
            ?: _messages.value.values.lastOrNull { it.convId == convId && !it.outgoing }?.name ?: "Unknown"
    }
}
