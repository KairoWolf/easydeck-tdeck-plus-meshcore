package com.easydeck.app

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * MeshCore companion protocol + EasyDeck Sync Protocol (EDS).
 * One frame per BLE write/notify, little-endian. See firmware/src/sync/EasyDeckProtocol.h.
 */
object Proto {
    // MeshCore commands
    const val CMD_APP_START = 1
    const val CMD_SEND_TXT_MSG = 2
    const val CMD_SEND_CHANNEL_TXT_MSG = 3
    const val CMD_GET_CONTACTS = 4
    const val CMD_SET_DEVICE_TIME = 6
    const val CMD_SEND_SELF_ADVERT = 7
    const val CMD_SYNC_NEXT_MESSAGE = 10
    const val CMD_GET_BATT_AND_STORAGE = 20
    const val CMD_GET_CHANNEL = 31

    // MeshCore responses / pushes
    const val RESP_OK = 0
    const val RESP_ERR = 1
    const val RESP_CONTACTS_START = 2
    const val RESP_CONTACT = 3
    const val RESP_END_OF_CONTACTS = 4
    const val RESP_SELF_INFO = 5
    const val RESP_SENT = 6
    const val RESP_NO_MORE_MESSAGES = 10
    const val RESP_BATT_AND_STORAGE = 12
    const val RESP_CHANNEL_INFO = 18
    const val PUSH_ADVERT = 0x80
    const val PUSH_SEND_CONFIRMED = 0x82
    const val PUSH_MSG_WAITING = 0x83
    const val PUSH_NEW_ADVERT = 0x8A

    // EasyDeck Sync Protocol
    const val EDS_VERSION = 1
    const val EDS_CMD_HELLO = 0xC0
    const val EDS_CMD_GET_SINCE = 0xC1
    const val EDS_CMD_MARK_READ = 0xC2
    const val EDS_RESP_HELLO = 0xC0
    const val EDS_RESP_RECORD = 0xC1
    const val EDS_RESP_END = 0xC2
    const val EDS_RESP_OK = 0xC3
    const val EDS_PUSH_NEW = 0xCE
    const val EDS_PUSH_UPDATE = 0xCF

    fun le(size: Int): ByteBuffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)

    fun appStart(): ByteArray {
        val name = "EasyDeck".toByteArray()
        return le(8 + name.size).put(CMD_APP_START.toByte()).put(3).put(ByteArray(6)).put(name).array()
    }
    fun setTime(epoch: Long): ByteArray = le(5).put(CMD_SET_DEVICE_TIME.toByte()).putInt(epoch.toInt()).array()
    fun getContacts(): ByteArray = byteArrayOf(CMD_GET_CONTACTS.toByte())
    fun getChannel(idx: Int): ByteArray = byteArrayOf(CMD_GET_CHANNEL.toByte(), idx.toByte())
    fun getBattery(): ByteArray = byteArrayOf(CMD_GET_BATT_AND_STORAGE.toByte())
    fun syncNext(): ByteArray = byteArrayOf(CMD_SYNC_NEXT_MESSAGE.toByte())
    fun advert(flood: Boolean): ByteArray = byteArrayOf(CMD_SEND_SELF_ADVERT.toByte(), if (flood) 1 else 0)

    fun sendDirect(pubkeyPrefix6: ByteArray, text: String, ts: Long): ByteArray {
        val t = text.toByteArray().take(150).toByteArray()
        return le(13 + t.size).put(CMD_SEND_TXT_MSG.toByte()).put(0).put(0).putInt(ts.toInt())
            .put(pubkeyPrefix6, 0, 6).put(t).array()
    }
    fun sendChannel(channelIdx: Int, text: String, ts: Long): ByteArray {
        val t = text.toByteArray().take(140).toByteArray()
        return le(7 + t.size).put(CMD_SEND_CHANNEL_TXT_MSG.toByte()).put(0).put(channelIdx.toByte())
            .putInt(ts.toInt()).put(t).array()
    }

    fun edsHello(): ByteArray = byteArrayOf(EDS_CMD_HELLO.toByte(), EDS_VERSION.toByte())
    fun edsGetSince(seq: Long, max: Int = 32): ByteArray =
        le(6).put(EDS_CMD_GET_SINCE.toByte()).putInt(seq.toInt()).put(max.toByte()).array()
    fun edsMarkRead(kind: Int, key: ByteArray): ByteArray =
        le(8).put(EDS_CMD_MARK_READ.toByte()).put(kind.toByte()).put(key, 0, 6).array()
}

private fun ByteArray.u8(i: Int) = this[i].toInt() and 0xFF
private fun ByteArray.u32(i: Int): Long = ByteBuffer.wrap(this, i, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL
private fun ByteArray.i32(i: Int): Int = ByteBuffer.wrap(this, i, 4).order(ByteOrder.LITTLE_ENDIAN).int
private fun ByteArray.cstr(off: Int, max: Int): String {
    var end = off
    while (end < size && end < off + max && this[end] != 0.toByte()) end++
    return String(this, off, end - off)
}
fun ByteArray.hex(n: Int = size): String = take(n).joinToString("") { "%02x".format(it) }

data class SelfInfo(val name: String, val pubkey: ByteArray, val freq: Float, val bw: Float, val sf: Int)

data class Contact(val pubkey: ByteArray, val type: Int, val flags: Int, val name: String,
                   val lastAdvert: Long, val lat: Double, val lon: Double, val lastMod: Long) {
    val key6: String get() = pubkey.hex(6)
    val favorite: Boolean get() = flags and 1 != 0
}

data class Channel(val idx: Int, val name: String, val secret: ByteArray) { val key6: String get() = secret.hex(6) }

/** One entry of the device's shared message history (EDS record). */
data class Message(
    val seq: Long, val storedAt: Long, val senderTs: Long, val kind: Int, val dir: Int,
    val status: Int, val origin: Int, val key: ByteArray, val channelIdx: Int, val flags: Int,
    val snr: Float, val pathLen: Int, val name: String, val text: String,
) {
    val convId: String get() = "$kind:${key.hex()}"
    val outgoing: Boolean get() = dir == 1
    val read: Boolean get() = flags and 1 != 0
    val silent: Boolean get() = flags and 4 != 0
    val sos: Boolean get() = flags and 2 != 0
    /** How it travelled: "heard by 3" for my group messages (repeaters that re-sent it),
     *  "direct" / "2 hops" for messages received over the radio, else "". */
    val routeNote: String get() = when {
        outgoing && kind == KIND_CHANNEL && pathLen in 1..254 -> "heard by $pathLen"
        !outgoing && origin == 0 && (pathLen == 0xFF || pathLen == 0) -> "direct"
        !outgoing && origin == 0 -> (pathLen and 0x3F).let { "$it hop" + if (it == 1) "" else "s" }
        else -> ""
    }
    companion object {
        const val KIND_DIRECT = 0
        const val KIND_CHANNEL = 1
        const val ST_PENDING = 1; const val ST_SENT = 2; const val ST_DELIVERED = 3; const val ST_FAILED = 4
    }
}

object Parse {
    fun selfInfo(f: ByteArray): SelfInfo? {
        if (f.size < 58) return null
        val pub = f.copyOfRange(4, 36)
        val freq = f.u32(48) / 1000f
        val bw = f.u32(52) / 1000f
        val sf = f.u8(56)
        val name = String(f, 58, f.size - 58)
        return SelfInfo(name, pub, freq, bw, sf)
    }

    fun contact(f: ByteArray): Contact? {
        if (f.size < 148) return null
        val pub = f.copyOfRange(1, 33)
        val type = f.u8(33); val flags = f.u8(34)
        val name = f.cstr(100, 32)
        return Contact(pub, type, flags, name, f.u32(132), f.i32(136) / 1e6, f.i32(140) / 1e6, f.u32(144))
    }

    fun channel(f: ByteArray): Channel? {
        if (f.size < 50) return null
        val name = f.cstr(2, 32)
        if (name.isEmpty()) return null
        return Channel(f.u8(1), name, f.copyOfRange(34, 50))
    }

    fun record(f: ByteArray): Message? {
        if (f.size < 28) return null
        val nameLen = f.u8(27)
        if (28 + nameLen > f.size) return null
        val name = String(f, 28, nameLen)
        val text = String(f, 28 + nameLen, f.size - 28 - nameLen)
        return Message(
            seq = f.u32(1), storedAt = f.u32(5), senderTs = f.u32(9), kind = f.u8(13), dir = f.u8(14),
            status = f.u8(15), origin = f.u8(16), key = f.copyOfRange(17, 23), channelIdx = f.u8(23),
            flags = f.u8(24), snr = f[25] / 4f, pathLen = f.u8(26), name = name, text = text,
        )
    }
}
