package com.superdro.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.superdro.app.serial.ConnectionState
import com.superdro.app.serial.Protocol
import com.superdro.app.viewmodel.DroViewModel

private val DarkBackground = Color(0xFF1A1A1A)
private val CardBg = Color(0xFF222222)
private val DroGreen = Color(0xFF00FF88)
private val DimGray = Color(0xFF666666)

data class ConfigParam(
    val key: String,
    val label: String,
    val category: String,
    val readOnly: Boolean = false
)

private val configParams = listOf(
    ConfigParam("spindle_ppr", "Encoder PPR", "Spindle"),
    ConfigParam("spindle_quadrature", "Quadrature multiplier", "Spindle"),
    ConfigParam("spindle_counts_per_rev", "Counts/rev", "Spindle", readOnly = true),
    ConfigParam("spindle_max_rpm", "Max RPM", "Spindle"),

    ConfigParam("z_leadscrew_pitch_mm", "Leadscrew pitch (mm)", "Z Axis"),
    ConfigParam("z_steps_per_rev", "Steps/rev", "Z Axis"),
    ConfigParam("z_belt_ratio", "Belt ratio", "Z Axis"),
    ConfigParam("z_steps_per_mm", "Steps/mm", "Z Axis", readOnly = true),
    ConfigParam("z_max_speed_mm_s", "Max speed (mm/s)", "Z Axis"),
    ConfigParam("z_accel_mm_s2", "Acceleration (mm/s2)", "Z Axis"),
    ConfigParam("z_backlash_mm", "Backlash (mm)", "Z Axis"),

    ConfigParam("x_scale_resolution_mm", "Scale resolution (mm)", "X Axis"),
    ConfigParam("x_is_diameter", "Diameter mode", "X Axis"),
)

@Composable
fun ConfigScreen(viewModel: DroViewModel) {
    val connectionState by viewModel.connectionState.collectAsState()
    val scrollState = rememberScrollState()

    // Local editable values
    val editValues = remember { mutableStateMapOf<String, String>() }
    val dirtyKeys = remember { mutableStateSetOf<String>() }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(DarkBackground)
            .padding(16.dp)
    ) {
        // Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Machine Configuration", color = Color.White, fontSize = 24.sp, fontWeight = FontWeight.Bold)

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                // Load from Pico
                TextButton(
                    onClick = {
                        viewModel.serialManager.send(Protocol.configListCommand())
                    },
                    enabled = connectionState == ConnectionState.CONNECTED,
                    colors = ButtonDefaults.textButtonColors(containerColor = Color(0xFF333333))
                ) {
                    Text("LOAD", color = if (connectionState == ConnectionState.CONNECTED) Color.White else DimGray)
                }

                // Save to Pico
                TextButton(
                    onClick = {
                        dirtyKeys.forEach { key ->
                            editValues[key]?.let { value ->
                                viewModel.serialManager.send(Protocol.configSetCommand(key, value))
                            }
                        }
                        viewModel.serialManager.send(Protocol.configSaveCommand())
                        dirtyKeys.clear()
                    },
                    enabled = connectionState == ConnectionState.CONNECTED && dirtyKeys.isNotEmpty(),
                    colors = ButtonDefaults.textButtonColors(containerColor = DroGreen.copy(alpha = 0.2f))
                ) {
                    Text("SAVE", color = if (dirtyKeys.isNotEmpty()) DroGreen else DimGray)
                }
            }
        }

        if (connectionState != ConnectionState.CONNECTED) {
            Spacer(modifier = Modifier.height(32.dp))
            Text(
                "Connect USB to configure",
                color = DimGray,
                fontSize = 18.sp,
                modifier = Modifier.align(Alignment.CenterHorizontally)
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Config categories
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(scrollState),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            val categories = configParams.groupBy { it.category }
            categories.forEach { (category, params) ->
                ConfigCategory(
                    category = category,
                    params = params,
                    values = editValues,
                    onValueChange = { key, value ->
                        editValues[key] = value
                        dirtyKeys.add(key)
                    }
                )
            }
        }
    }
}

@Composable
private fun ConfigCategory(
    category: String,
    params: List<ConfigParam>,
    values: Map<String, String>,
    onValueChange: (String, String) -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(CardBg, RoundedCornerShape(8.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(category, color = DroGreen, fontSize = 18.sp, fontWeight = FontWeight.Bold)

        params.forEach { param ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = param.label,
                    color = Color.White,
                    fontSize = 14.sp,
                    modifier = Modifier.weight(1f)
                )

                if (param.readOnly) {
                    Text(
                        text = values[param.key] ?: "—",
                        color = DimGray,
                        fontSize = 14.sp
                    )
                } else {
                    OutlinedTextField(
                        value = values[param.key] ?: "",
                        onValueChange = { onValueChange(param.key, it) },
                        modifier = Modifier.width(120.dp),
                        singleLine = true,
                        textStyle = LocalTextStyle.current.copy(
                            color = Color.White,
                            fontSize = 14.sp
                        ),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = DroGreen,
                            unfocusedBorderColor = DimGray
                        )
                    )
                }
            }
        }
    }
}
