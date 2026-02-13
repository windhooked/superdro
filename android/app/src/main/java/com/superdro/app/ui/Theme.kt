package com.superdro.app.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkScheme = darkColorScheme(
    primary = Color(0xFF00FF88),
    onPrimary = Color.Black,
    background = Color(0xFF1A1A1A),
    onBackground = Color.White,
    surface = Color(0xFF222222),
    onSurface = Color.White,
)

@Composable
fun SuperDROTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkScheme,
        content = content
    )
}
