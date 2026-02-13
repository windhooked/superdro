package com.superdro.app.serial

import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {

    @Test
    fun `parseStatus parses valid idle status`() {
        val json = """{"pos":{"x":12.450,"z":-35.200},"rpm":820,"state":"idle","fh":false}"""
        val state = Protocol.parseStatus(json)
        assertNotNull(state)
        assertEquals(12.450f, state!!.xPosMm, 0.001f)
        assertEquals(-35.200f, state.zPosMm, 0.001f)
        assertEquals(820f, state.rpm, 1f)
        assertEquals("idle", state.state)
        assertFalse(state.feedHold)
    }

    @Test
    fun `parseStatus parses alarm state`() {
        val json = """{"pos":{"x":0.0,"z":0.0},"rpm":0,"state":"alarm","fh":false}"""
        val state = Protocol.parseStatus(json)
        assertNotNull(state)
        assertEquals("alarm", state!!.state)
        assertEquals(0f, state.rpm, 0.01f)
    }

    @Test
    fun `parseStatus parses threading state with extras`() {
        val json = """{"pos":{"x":12.0,"z":-20.0},"rpm":500,"state":"threading","pitch":1.5,"dir":"rh","pass":3,"fh":false}"""
        val state = Protocol.parseStatus(json)
        assertNotNull(state)
        assertEquals("threading", state!!.state)
        assertEquals(500f, state.rpm, 1f)
    }

    @Test
    fun `parseStatus returns null for ACK messages`() {
        val json = """{"ack":"zero","ok":true}"""
        val state = Protocol.parseStatus(json)
        assertNull(state)
    }

    @Test
    fun `parseStatus returns null for malformed JSON`() {
        val state = Protocol.parseStatus("not json at all")
        assertNull(state)
    }

    @Test
    fun `parseStatus returns null for empty string`() {
        assertNull(Protocol.parseStatus(""))
    }

    @Test
    fun `parseStatus handles missing optional fields`() {
        val json = """{"pos":{"x":1.0,"z":2.0},"rpm":100}"""
        val state = Protocol.parseStatus(json)
        assertNotNull(state)
        assertEquals("idle", state!!.state) // default
        assertFalse(state.feedHold) // default
    }

    @Test
    fun `parseStatus handles negative positions`() {
        val json = """{"pos":{"x":-50.5,"z":-100.25},"rpm":0,"state":"idle","fh":false}"""
        val state = Protocol.parseStatus(json)
        assertNotNull(state)
        assertEquals(-50.5f, state!!.xPosMm, 0.01f)
        assertEquals(-100.25f, state.zPosMm, 0.01f)
    }

    @Test
    fun `parseStatus handles high RPM`() {
        val json = """{"pos":{"x":0,"z":0},"rpm":3500,"state":"idle","fh":false}"""
        val state = Protocol.parseStatus(json)
        assertEquals(3500f, state!!.rpm, 1f)
    }

    @Test
    fun `zeroCommand generates correct JSON`() {
        assertEquals("""{"cmd":"zero","axis":"x"}""", Protocol.zeroCommand("x"))
        assertEquals("""{"cmd":"zero","axis":"z"}""", Protocol.zeroCommand("z"))
    }

    @Test
    fun `presetCommand generates correct JSON`() {
        val cmd = Protocol.presetCommand("x", 25.0f)
        assertTrue(cmd.contains(""""cmd":"preset""""))
        assertTrue(cmd.contains(""""axis":"x""""))
        assertTrue(cmd.contains(""""value":25.0"""))
    }

    @Test
    fun `configGetCommand generates correct JSON`() {
        val cmd = Protocol.configGetCommand("spindle_ppr")
        assertEquals("""{"cmd":"config_get","key":"spindle_ppr"}""", cmd)
    }

    @Test
    fun `configSetCommand generates correct JSON`() {
        val cmd = Protocol.configSetCommand("z_leadscrew_pitch_mm", "4.0")
        assertTrue(cmd.contains(""""cmd":"config_set""""))
        assertTrue(cmd.contains(""""key":"z_leadscrew_pitch_mm""""))
    }

    @Test
    fun `configSaveCommand generates correct JSON`() {
        assertEquals("""{"cmd":"config_save"}""", Protocol.configSaveCommand())
    }

    @Test
    fun `configListCommand generates correct JSON`() {
        assertEquals("""{"cmd":"config_list"}""", Protocol.configListCommand())
    }
}
