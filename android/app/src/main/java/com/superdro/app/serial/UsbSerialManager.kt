package com.superdro.app.serial

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.IOException

enum class ConnectionState {
    DISCONNECTED, CONNECTING, CONNECTED, ERROR
}

class UsbSerialManager(private val context: Context) {

    companion object {
        private const val ACTION_USB_PERMISSION = "com.superdro.app.USB_PERMISSION"
        private const val BAUD_RATE = 115200
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var serialPort: UsbSerialPort? = null
    private var connection: UsbDeviceConnection? = null
    private var ioManager: SerialInputOutputManager? = null

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    var onDataReceived: ((String) -> Unit)? = null

    private val lineBuffer = StringBuilder()

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> {
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
                    if (granted && device != null) {
                        openDevice(device)
                    } else {
                        _connectionState.value = ConnectionState.ERROR
                    }
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    disconnect()
                }
            }
        }
    }

    fun register() {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        context.registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
    }

    fun unregister() {
        disconnect()
        try {
            context.unregisterReceiver(usbReceiver)
        } catch (_: Exception) {}
    }

    fun connect() {
        _connectionState.value = ConnectionState.CONNECTING

        val availableDrivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        if (availableDrivers.isEmpty()) {
            _connectionState.value = ConnectionState.ERROR
            return
        }

        val driver = availableDrivers[0]
        val device = driver.device

        if (!usbManager.hasPermission(device)) {
            val intent = PendingIntent.getBroadcast(
                context, 0,
                Intent(ACTION_USB_PERMISSION),
                PendingIntent.FLAG_IMMUTABLE
            )
            usbManager.requestPermission(device, intent)
            return
        }

        openDevice(device)
    }

    private fun openDevice(device: UsbDevice) {
        try {
            val conn = usbManager.openDevice(device) ?: run {
                _connectionState.value = ConnectionState.ERROR
                return
            }
            connection = conn

            val driver = CdcAcmSerialDriver(device)
            val port = driver.ports[0]
            port.open(conn)
            port.setParameters(BAUD_RATE, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            serialPort = port

            val listener = object : SerialInputOutputManager.Listener {
                override fun onNewData(data: ByteArray) {
                    processIncomingData(String(data))
                }
                override fun onRunError(e: Exception) {
                    _connectionState.value = ConnectionState.ERROR
                    disconnect()
                }
            }

            ioManager = SerialInputOutputManager(port, listener).also {
                Thread(it).start()
            }

            _connectionState.value = ConnectionState.CONNECTED
        } catch (e: IOException) {
            _connectionState.value = ConnectionState.ERROR
            disconnect()
        }
    }

    private fun processIncomingData(data: String) {
        lineBuffer.append(data)
        var newlineIdx = lineBuffer.indexOf('\n')
        while (newlineIdx >= 0) {
            val line = lineBuffer.substring(0, newlineIdx).trim()
            if (line.isNotEmpty()) {
                onDataReceived?.invoke(line)
            }
            lineBuffer.delete(0, newlineIdx + 1)
            newlineIdx = lineBuffer.indexOf('\n')
        }
    }

    fun send(json: String) {
        try {
            serialPort?.write("$json\n".toByteArray(), 100)
        } catch (_: IOException) {}
    }

    fun disconnect() {
        ioManager?.stop()
        ioManager = null
        try { serialPort?.close() } catch (_: Exception) {}
        serialPort = null
        try { connection?.close() } catch (_: Exception) {}
        connection = null
        _connectionState.value = ConnectionState.DISCONNECTED
    }
}
