package com.superdro.app.model

data class MachineState(
    val xPosMm: Float = 0f,
    val zPosMm: Float = 0f,
    val rpm: Float = 0f,
    val state: String = "idle",
    val feedHold: Boolean = false
)

data class ToolOffset(
    val toolNumber: Int,
    val xOffset: Float = 0f,
    val zOffset: Float = 0f
)

enum class UnitMode { METRIC, IMPERIAL }
enum class XDisplayMode { DIAMETER, RADIUS }
