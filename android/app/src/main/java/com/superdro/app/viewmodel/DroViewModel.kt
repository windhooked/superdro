package com.superdro.app.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.superdro.app.model.MachineState
import com.superdro.app.model.ToolOffset
import com.superdro.app.model.UnitMode
import com.superdro.app.model.XDisplayMode
import com.superdro.app.serial.ConnectionState
import com.superdro.app.serial.Protocol
import com.superdro.app.serial.UsbSerialManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class DroViewModel(application: Application) : AndroidViewModel(application) {

    val serialManager = UsbSerialManager(application)

    private val _machineState = MutableStateFlow(MachineState())
    val machineState: StateFlow<MachineState> = _machineState.asStateFlow()

    private val _unitMode = MutableStateFlow(UnitMode.METRIC)
    val unitMode: StateFlow<UnitMode> = _unitMode.asStateFlow()

    private val _xDisplayMode = MutableStateFlow(XDisplayMode.DIAMETER)
    val xDisplayMode: StateFlow<XDisplayMode> = _xDisplayMode.asStateFlow()

    private val _toolOffsets = MutableStateFlow<List<ToolOffset>>(emptyList())
    val toolOffsets: StateFlow<List<ToolOffset>> = _toolOffsets.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = serialManager.connectionState

    init {
        serialManager.onDataReceived = { line ->
            Protocol.parseStatus(line)?.let { status ->
                _machineState.value = status
            }
        }
    }

    fun connect() = serialManager.connect()
    fun disconnect() = serialManager.disconnect()

    fun zeroAxis(axis: String) {
        serialManager.send(Protocol.zeroCommand(axis))
    }

    fun presetAxis(axis: String, value: Float) {
        // Convert from display units to mm if needed
        val mmValue = when (_unitMode.value) {
            UnitMode.METRIC -> value
            UnitMode.IMPERIAL -> value * 25.4f
        }
        // Convert from diameter to radius if X axis in diameter mode
        val finalValue = if (axis == "x" && _xDisplayMode.value == XDisplayMode.DIAMETER) {
            mmValue / 2f
        } else {
            mmValue
        }
        serialManager.send(Protocol.presetCommand(axis, finalValue))
    }

    fun toggleUnitMode() {
        _unitMode.value = when (_unitMode.value) {
            UnitMode.METRIC -> UnitMode.IMPERIAL
            UnitMode.IMPERIAL -> UnitMode.METRIC
        }
    }

    fun toggleXDisplayMode() {
        _xDisplayMode.value = when (_xDisplayMode.value) {
            XDisplayMode.DIAMETER -> XDisplayMode.RADIUS
            XDisplayMode.RADIUS -> XDisplayMode.DIAMETER
        }
    }

    // Convert raw mm position to display value
    fun displayX(rawMm: Float): Float {
        val scaled = when (_xDisplayMode.value) {
            XDisplayMode.DIAMETER -> rawMm * 2f
            XDisplayMode.RADIUS -> rawMm
        }
        return when (_unitMode.value) {
            UnitMode.METRIC -> scaled
            UnitMode.IMPERIAL -> scaled / 25.4f
        }
    }

    fun displayZ(rawMm: Float): Float {
        return when (_unitMode.value) {
            UnitMode.METRIC -> rawMm
            UnitMode.IMPERIAL -> rawMm / 25.4f
        }
    }

    fun displayUnit(): String = when (_unitMode.value) {
        UnitMode.METRIC -> "mm"
        UnitMode.IMPERIAL -> "in"
    }

    override fun onCleared() {
        serialManager.unregister()
        super.onCleared()
    }
}
