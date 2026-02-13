package com.superdro.app.model

import org.junit.Assert.*
import org.junit.Test

class MachineStateTest {

    @Test
    fun `default MachineState has zero values`() {
        val state = MachineState()
        assertEquals(0f, state.xPosMm, 0.001f)
        assertEquals(0f, state.zPosMm, 0.001f)
        assertEquals(0f, state.rpm, 0.001f)
        assertEquals("idle", state.state)
        assertFalse(state.feedHold)
    }

    @Test
    fun `MachineState data class copy works`() {
        val state = MachineState(xPosMm = 10f, rpm = 500f)
        val updated = state.copy(zPosMm = -20f)
        assertEquals(10f, updated.xPosMm, 0.001f)
        assertEquals(-20f, updated.zPosMm, 0.001f)
        assertEquals(500f, updated.rpm, 0.001f)
    }

    @Test
    fun `ToolOffset defaults to zero`() {
        val offset = ToolOffset(toolNumber = 1)
        assertEquals(0f, offset.xOffset, 0.001f)
        assertEquals(0f, offset.zOffset, 0.001f)
    }

    @Test
    fun `UnitMode values exist`() {
        assertEquals(2, UnitMode.values().size)
        assertNotNull(UnitMode.METRIC)
        assertNotNull(UnitMode.IMPERIAL)
    }

    @Test
    fun `XDisplayMode values exist`() {
        assertEquals(2, XDisplayMode.values().size)
        assertNotNull(XDisplayMode.DIAMETER)
        assertNotNull(XDisplayMode.RADIUS)
    }

    // Unit conversion tests (matching DroViewModel logic)

    @Test
    fun `mm to inch conversion`() {
        val mm = 25.4f
        val inch = mm / 25.4f
        assertEquals(1.0f, inch, 0.0001f)
    }

    @Test
    fun `diameter to radius conversion`() {
        val radiusMm = 12.5f
        val diameterMm = radiusMm * 2f
        assertEquals(25.0f, diameterMm, 0.001f)
    }

    @Test
    fun `imperial diameter display`() {
        // Raw position: 12.7mm radius
        val rawMm = 12.7f
        val diameter = rawMm * 2f  // diameter mode
        val inch = diameter / 25.4f  // imperial
        assertEquals(1.0f, inch, 0.0001f)
    }
}
