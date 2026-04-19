package com.hackwa.companion.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

// ── HackWa futuristic purple-blue color scheme ──────────────
val HackPurple     = Color(0xFFBB86FC)
val HackPurpleDark = Color(0xFF9C6FE0)
val HackBlue       = Color(0xFF448AFF)
val HackDark       = Color(0xFF0D1117)
val HackSurface    = Color(0xFF161B22)
val HackCard       = Color(0xFF21262D)
val HackText       = Color(0xFFE6EDF3)
val HackDim        = Color(0xFF8B949E)
val HackRed        = Color(0xFFFF5252)
val HackOrange     = Color(0xFFFF9100)

// ── Backward-compat aliases (screens still reference these) ──
val HackGreen      = HackPurple
val HackGreenDark  = HackPurpleDark
val HackCyan       = HackBlue

private val DarkColorScheme = darkColorScheme(
    primary          = HackPurple,
    onPrimary        = HackDark,
    primaryContainer = HackPurpleDark,
    secondary        = HackBlue,
    onSecondary      = HackDark,
    background       = HackDark,
    surface          = HackSurface,
    surfaceVariant   = HackCard,
    onBackground     = HackText,
    onSurface        = HackText,
    onSurfaceVariant = HackDim,
    error            = HackRed,
    outline          = HackDim,
)

@Composable
fun HackWaTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        typography  = Typography(),
        content     = content,
    )
}
