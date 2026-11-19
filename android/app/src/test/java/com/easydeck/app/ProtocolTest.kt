package com.easydeck.app

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Frames built exactly like the firmware does (SyncInterface::encodeRecord, MyMesh). */
class ProtocolTest {
    private fun le(n: Int) = ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN)

    @Test fun parsesEdsRecord() {
        val name = "Sarah K".toByteArray(); val text = "Around 3 👍".toByteArray()
        val f = le(28 + name.size + text.size).put(0xC1.toByte()).putInt(42).putInt(1795600000).putInt(1795599990)
            .put(0).put(0).put(0).put(0).put(byteArrayOf(1, 2, 3, 4, 5, 6)).put(0).put(1).put(26).put(2)
            .put(name.size.toByte()).put(name).put(text).array()
        val m = Parse.record(f)!!
        assertEquals(42L, m.seq); assertEquals(1795600000L, m.storedAt); assertEquals(0, m.kind)
        assertFalse(m.outgoing); assertTrue(m.read); assertEquals(6.5f, m.snr, 0.01f); assertEquals(2, m.pathLen)
        assertEquals("Sarah K", m.name); assertEquals("Around 3 👍", m.text); assertEquals("0:010203040506", m.convId)
    }

    @Test fun parsesContact() {
        val pub = ByteArray(32) { it.toByte() }
        val f = le(148).put(3).put(pub).put(1).put(1).put(0xFF.toByte()).put(ByteArray(64))
            .put("Mum".toByteArray()).put(ByteArray(29)).putInt(1795000000).putInt(-32183000).putInt(152517000).putInt(1795000100).array()
        val c = Parse.contact(f)!!
        assertEquals("Mum", c.name); assertTrue(c.favorite); assertEquals(-32.183, c.lat, 1e-6); assertEquals("000102030405", c.key6)
    }

    @Test fun parsesChannel() {
        val f = le(50).put(18).put(2).put("#nsw".toByteArray()).put(ByteArray(28)).put(ByteArray(16) { 7 }).array()
        val ch = Parse.channel(f)!!
        assertEquals(2, ch.idx); assertEquals("#nsw", ch.name); assertEquals("070707070707", ch.key6)
    }

    @Test fun buildsSendFrames() {
        val d = Proto.sendDirect(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8), "hi", 1000)
        assertEquals(15, d.size); assertEquals(2, d[0].toInt()); assertEquals('h'.code.toByte(), d[13])
        val g = Proto.edsGetSince(300, 32)
        assertEquals(0xC1.toByte(), g[0]); assertEquals(300, le(4).put(g, 1, 4).getInt(0)); assertEquals(32, g[5].toInt())
    }
}
