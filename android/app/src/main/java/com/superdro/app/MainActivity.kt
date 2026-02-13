package com.superdro.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.superdro.app.ui.ConfigScreen
import com.superdro.app.ui.DroScreen
import com.superdro.app.ui.SuperDROTheme
import com.superdro.app.viewmodel.DroViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: DroViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        viewModel.serialManager.register()

        setContent {
            SuperDROTheme {
                MainContent(viewModel)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        viewModel.connect()
    }

    override fun onDestroy() {
        viewModel.serialManager.unregister()
        super.onDestroy()
    }
}

@Composable
private fun MainContent(viewModel: DroViewModel) {
    var currentTab by remember { mutableIntStateOf(0) }

    Column(modifier = Modifier.fillMaxSize()) {
        // Main content
        Box(modifier = Modifier.weight(1f)) {
            when (currentTab) {
                0 -> DroScreen(viewModel)
                1 -> ConfigScreen(viewModel)
            }
        }

        // Bottom tab bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111111))
                .padding(4.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            TabButton("DRO", currentTab == 0) { currentTab = 0 }
            TabButton("CONFIG", currentTab == 1) { currentTab = 1 }
        }
    }
}

@Composable
private fun RowScope.TabButton(label: String, selected: Boolean, onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        modifier = Modifier.weight(1f),
        colors = ButtonDefaults.textButtonColors(
            containerColor = if (selected) Color(0xFF333333) else Color.Transparent
        )
    ) {
        Text(
            text = label,
            color = if (selected) Color(0xFF00FF88) else Color(0xFF666666),
            fontSize = 14.sp
        )
    }
}
