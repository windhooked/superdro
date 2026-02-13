package com.superdro.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.superdro.app.model.UnitMode
import com.superdro.app.model.XDisplayMode
import com.superdro.app.serial.ConnectionState
import com.superdro.app.viewmodel.DroViewModel

// Workshop-visible colors
private val DarkBackground = Color(0xFF1A1A1A)
private val DroGreen = Color(0xFF00FF88)
private val DroBlue = Color(0xFF44BBFF)
private val DroYellow = Color(0xFFFFDD44)
private val DroRed = Color(0xFFFF4444)
private val DimGray = Color(0xFF666666)
private val ButtonBg = Color(0xFF333333)

@Composable
fun DroScreen(viewModel: DroViewModel) {
    val machineState by viewModel.machineState.collectAsState()
    val unitMode by viewModel.unitMode.collectAsState()
    val xDisplayMode by viewModel.xDisplayMode.collectAsState()
    val connectionState by viewModel.connectionState.collectAsState()

    var showPresetDialog by remember { mutableStateOf<String?>(null) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(DarkBackground)
            .padding(16.dp)
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            // Top bar: connection status + controls
            TopBar(
                connectionState = connectionState,
                unitMode = unitMode,
                xDisplayMode = xDisplayMode,
                onConnect = { viewModel.connect() },
                onToggleUnits = { viewModel.toggleUnitMode() },
                onToggleXMode = { viewModel.toggleXDisplayMode() }
            )

            Spacer(modifier = Modifier.height(16.dp))

            // DRO readouts
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                // X axis
                DroReadout(
                    modifier = Modifier.weight(1f),
                    label = "X",
                    sublabel = if (xDisplayMode == XDisplayMode.DIAMETER) "DIA" else "RAD",
                    value = viewModel.displayX(machineState.xPosMm),
                    unit = viewModel.displayUnit(),
                    color = DroGreen,
                    onZero = { viewModel.zeroAxis("x") },
                    onPreset = { showPresetDialog = "x" }
                )

                // Z axis
                DroReadout(
                    modifier = Modifier.weight(1f),
                    label = "Z",
                    sublabel = null,
                    value = viewModel.displayZ(machineState.zPosMm),
                    unit = viewModel.displayUnit(),
                    color = DroBlue,
                    onZero = { viewModel.zeroAxis("z") },
                    onPreset = { showPresetDialog = "z" }
                )

                // RPM
                RpmReadout(
                    modifier = Modifier.weight(0.6f),
                    rpm = machineState.rpm,
                    state = machineState.state
                )
            }
        }

        // State indicator
        if (machineState.state == "alarm") {
            Text(
                text = "ALARM",
                color = DroRed,
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.align(Alignment.BottomCenter)
            )
        }
    }

    // Preset dialog
    showPresetDialog?.let { axis ->
        PresetDialog(
            axis = axis,
            unit = viewModel.displayUnit(),
            onDismiss = { showPresetDialog = null },
            onConfirm = { value ->
                viewModel.presetAxis(axis, value)
                showPresetDialog = null
            }
        )
    }
}

@Composable
private fun TopBar(
    connectionState: ConnectionState,
    unitMode: UnitMode,
    xDisplayMode: XDisplayMode,
    onConnect: () -> Unit,
    onToggleUnits: () -> Unit,
    onToggleXMode: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Connection indicator
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.clickable { onConnect() }
        ) {
            val (statusColor, statusText) = when (connectionState) {
                ConnectionState.CONNECTED -> DroGreen to "USB Connected"
                ConnectionState.CONNECTING -> DroYellow to "Connecting..."
                ConnectionState.DISCONNECTED -> DimGray to "Disconnected"
                ConnectionState.ERROR -> DroRed to "Error"
            }
            Box(
                modifier = Modifier
                    .size(12.dp)
                    .background(statusColor, RoundedCornerShape(6.dp))
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(text = statusText, color = statusColor, fontSize = 14.sp)
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            // Unit toggle
            TextButton(
                onClick = onToggleUnits,
                colors = ButtonDefaults.textButtonColors(containerColor = ButtonBg)
            ) {
                Text(
                    text = if (unitMode == UnitMode.METRIC) "mm" else "in",
                    color = Color.White,
                    fontSize = 16.sp
                )
            }

            // X display mode toggle
            TextButton(
                onClick = onToggleXMode,
                colors = ButtonDefaults.textButtonColors(containerColor = ButtonBg)
            ) {
                Text(
                    text = if (xDisplayMode == XDisplayMode.DIAMETER) "DIA" else "RAD",
                    color = Color.White,
                    fontSize = 16.sp
                )
            }
        }
    }
}

@Composable
private fun DroReadout(
    modifier: Modifier,
    label: String,
    sublabel: String?,
    value: Float,
    unit: String,
    color: Color,
    onZero: () -> Unit,
    onPreset: () -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxHeight()
            .background(Color(0xFF222222), RoundedCornerShape(8.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.SpaceBetween
    ) {
        // Label
        Row {
            Text(text = label, color = color, fontSize = 28.sp, fontWeight = FontWeight.Bold)
            if (sublabel != null) {
                Spacer(modifier = Modifier.width(8.dp))
                Text(text = sublabel, color = DimGray, fontSize = 16.sp,
                    modifier = Modifier.align(Alignment.Bottom))
            }
        }

        // Value
        Text(
            text = String.format(if (unit == "in") "%+.4f" else "%+.3f", value),
            color = color,
            fontSize = 64.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.End,
            modifier = Modifier.fillMaxWidth()
        )

        // Unit label
        Text(
            text = unit,
            color = DimGray,
            fontSize = 18.sp,
            textAlign = TextAlign.End,
            modifier = Modifier.fillMaxWidth()
        )

        // Buttons
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            TextButton(
                onClick = onZero,
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.textButtonColors(containerColor = ButtonBg)
            ) {
                Text("ZERO", color = Color.White, fontSize = 16.sp)
            }
            TextButton(
                onClick = onPreset,
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.textButtonColors(containerColor = ButtonBg)
            ) {
                Text("SET", color = Color.White, fontSize = 16.sp)
            }
        }
    }
}

@Composable
private fun RpmReadout(modifier: Modifier, rpm: Float, state: String) {
    Column(
        modifier = modifier
            .fillMaxHeight()
            .background(Color(0xFF222222), RoundedCornerShape(8.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.SpaceBetween,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(text = "RPM", color = DroYellow, fontSize = 28.sp, fontWeight = FontWeight.Bold)

        Text(
            text = String.format("%.0f", rpm),
            color = DroYellow,
            fontSize = 56.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth()
        )

        // State
        Text(
            text = state.uppercase(),
            color = if (state == "alarm") DroRed else DimGray,
            fontSize = 16.sp,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
private fun PresetDialog(
    axis: String,
    unit: String,
    onDismiss: () -> Unit,
    onConfirm: (Float) -> Unit
) {
    var text by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Color(0xFF2A2A2A),
        title = {
            Text("Set ${axis.uppercase()} Position", color = Color.White)
        },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                label = { Text("Value ($unit)") },
                singleLine = true,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedTextColor = Color.White,
                    unfocusedTextColor = Color.White
                )
            )
        },
        confirmButton = {
            TextButton(onClick = {
                text.toFloatOrNull()?.let { onConfirm(it) }
            }) {
                Text("SET", color = DroGreen)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel", color = DimGray)
            }
        }
    )
}
