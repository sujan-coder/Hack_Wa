package com.hackwa.companion.ui.screens

import android.annotation.SuppressLint
import android.bluetooth.le.ScanResult
import androidx.compose.animation.*
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.hackwa.companion.ble.BleManager
import com.hackwa.companion.ui.theme.*
import kotlinx.coroutines.delay

/* ═══════════════════════════════════════════════════════════════
 *  Scan / Connect Screen
 * ═══════════════════════════════════════════════════════════════*/
@SuppressLint("MissingPermission")
@Composable
fun ScanScreen(
    connectionState: BleManager.ConnectionState,
    scanResults: List<ScanResult>,
    onStartScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnect: (ScanResult) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(48.dp))

        // Logo
        Box(
            modifier = Modifier
                .size(100.dp)
                .clip(CircleShape)
                .background(
                    Brush.linearGradient(listOf(HackGreen, HackCyan))
                ),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Filled.Watch,
                contentDescription = null,
                tint = HackDark,
                modifier = Modifier.size(52.dp),
            )
        }

        Spacer(Modifier.height(16.dp))

        Text(
            "HackWa",
            style = MaterialTheme.typography.headlineLarge.copy(
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = HackGreen,
            ),
        )
        Text(
            "Companion",
            style = MaterialTheme.typography.titleMedium.copy(color = HackDim),
        )

        Spacer(Modifier.height(32.dp))

        when (connectionState) {
            is BleManager.ConnectionState.Disconnected -> {
                Button(
                    onClick = onStartScan,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp),
                    shape = RoundedCornerShape(16.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = HackGreen),
                ) {
                    Icon(Icons.Filled.BluetoothSearching, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Scan for Watch", fontWeight = FontWeight.Bold, color = HackDark)
                }
            }

            is BleManager.ConnectionState.Scanning -> {
                CircularProgressIndicator(color = HackGreen)
                Spacer(Modifier.height(12.dp))
                Text("Scanning...", color = HackDim)
                Spacer(Modifier.height(8.dp))
                TextButton(onClick = onStopScan) {
                    Text("Cancel", color = HackRed)
                }
            }

            is BleManager.ConnectionState.Connecting -> {
                CircularProgressIndicator(color = HackCyan)
                Spacer(Modifier.height(12.dp))
                Text("Connecting...", color = HackDim)
            }

            else -> {} // Connected handled by navigation
        }

        Spacer(Modifier.height(16.dp))

        // Scan results list
        if (scanResults.isNotEmpty()) {
            Text(
                "Devices Found",
                style = MaterialTheme.typography.titleSmall,
                color = HackDim,
                modifier = Modifier.padding(bottom = 8.dp),
            )

            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(scanResults) { result ->
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onConnect(result) },
                        shape = RoundedCornerShape(12.dp),
                        colors = CardDefaults.cardColors(containerColor = HackCard),
                    ) {
                        Row(
                            modifier = Modifier.padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(
                                Icons.Filled.Bluetooth,
                                contentDescription = null,
                                tint = HackCyan,
                            )
                            Spacer(Modifier.width(12.dp))
                            Column(Modifier.weight(1f)) {
                                Text(
                                    result.device.name ?: "Unknown",
                                    fontWeight = FontWeight.SemiBold,
                                    color = HackText,
                                )
                                Text(
                                    result.device.address,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = HackDim,
                                    fontFamily = FontFamily.Monospace,
                                )
                            }
                            Text(
                                "${result.rssi} dBm",
                                style = MaterialTheme.typography.bodySmall,
                                color = HackDim,
                            )
                        }
                    }
                }
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Dashboard (main connected view)
 * ═══════════════════════════════════════════════════════════════*/
@Composable
fun DashboardScreen(
    deviceName: String,
    onSyncTime: () -> Unit,
    onDisconnect: () -> Unit,
    onNavigate: (String) -> Unit,
    onFindWatch: () -> Unit,
    logs: List<String>,
) {
    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // ── Header ──
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        "HackWa",
                        style = MaterialTheme.typography.headlineMedium.copy(
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace,
                            color = HackGreen,
                        ),
                    )
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(HackGreen)
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "Connected to $deviceName",
                            style = MaterialTheme.typography.bodySmall,
                            color = HackDim,
                        )
                    }
                }
                IconButton(onClick = onDisconnect) {
                    Icon(Icons.Filled.BluetoothDisabled, null, tint = HackRed)
                }
            }
        }

        // ── Quick Actions Row ──
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                QuickActionCard(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Filled.AccessTime,
                    label = "Sync Time",
                    color = HackGreen,
                    onClick = onSyncTime,
                )
                QuickActionCard(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Filled.Notifications,
                    label = "Notify",
                    color = HackCyan,
                    onClick = { onNavigate("notify") },
                )
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                QuickActionCard(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Filled.Key,
                    label = "Passwords",
                    color = HackOrange,
                    onClick = { onNavigate("passwords") },
                )
                QuickActionCard(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Filled.Settings,
                    label = "Settings",
                    color = HackDim,
                    onClick = { onNavigate("settings") },
                )
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                QuickActionCard(
                    modifier = Modifier.weight(1f),
                    icon = Icons.Filled.Watch,
                    label = "Find Watch",
                    color = HackPurple,
                    onClick = onFindWatch,
                )
                Spacer(modifier = Modifier.weight(1f))
            }
        }

        // ── Sensor Dashboard (placeholder) ──
        item {
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
                modifier = Modifier.fillMaxWidth(),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Sensors, null, tint = HackCyan)
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Sensor Dashboard",
                            fontWeight = FontWeight.SemiBold,
                            color = HackText,
                        )
                    }
                    Spacer(Modifier.height(12.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceEvenly,
                    ) {
                        SensorGauge("TEMP", "--°C", HackGreen)
                        SensorGauge("HUMID", "--%", HackCyan)
                        SensorGauge("BATT", "85%", HackOrange)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Connect sensors to see live data",
                        style = MaterialTheme.typography.bodySmall,
                        color = HackDim,
                        modifier = Modifier.fillMaxWidth(),
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }

        // ── BLE Log ──
        item {
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
                modifier = Modifier.fillMaxWidth(),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Terminal, null, tint = HackGreen)
                        Spacer(Modifier.width(8.dp))
                        Text("BLE Log", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(8.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 200.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(HackDark)
                            .padding(8.dp),
                    ) {
                        if (logs.isEmpty()) {
                            Text("No activity yet", color = HackDim, fontSize = 12.sp)
                        } else {
                            LazyColumn {
                                items(logs.reversed()) { line ->
                                    Text(
                                        line,
                                        fontFamily = FontFamily.Monospace,
                                        fontSize = 11.sp,
                                        color = HackGreen,
                                        lineHeight = 16.sp,
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ── Reusable components ──────────────────────────────────────

@Composable
fun QuickActionCard(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    color: Color,
    onClick: () -> Unit,
) {
    Card(
        modifier = modifier
            .height(100.dp)
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = HackCard),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(icon, contentDescription = label, tint = color, modifier = Modifier.size(32.dp))
            Spacer(Modifier.height(8.dp))
            Text(label, style = MaterialTheme.typography.bodyMedium, color = HackText)
        }
    }
}

@Composable
fun SensorGauge(label: String, value: String, color: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            value,
            fontSize = 20.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            color = color,
        )
        Text(
            label,
            fontSize = 10.sp,
            color = HackDim,
            letterSpacing = 2.sp,
        )
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Find My Phone — Full-screen alarm overlay
 * ═══════════════════════════════════════════════════════════════*/
@Composable
fun FindPhoneAlarmOverlay(onStop: () -> Unit) {
    // Pulsing animation
    var pulse by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        while (true) {
            pulse = !pulse
            delay(500)
        }
    }
    val scale by animateFloatAsState(
        targetValue = if (pulse) 1.2f else 0.9f,
        label = "pulse",
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xE6000000)),  // dim background
        contentAlignment = Alignment.Center,
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            // Pulsing phone icon
            Icon(
                Icons.Filled.PhoneAndroid,
                contentDescription = "Ringing",
                tint = HackGreen,
                modifier = Modifier
                    .size(120.dp)
                    .scale(scale),
            )

            Spacer(Modifier.height(16.dp))

            Text(
                "📱  FIND MY PHONE",
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = HackGreen,
            )

            Spacer(Modifier.height(8.dp))

            Text(
                "Triggered from your watch",
                fontSize = 14.sp,
                color = HackDim,
            )

            Spacer(Modifier.height(48.dp))

            // Big STOP button
            Button(
                onClick = onStop,
                modifier = Modifier
                    .fillMaxWidth(0.7f)
                    .height(64.dp),
                shape = RoundedCornerShape(20.dp),
                colors = ButtonDefaults.buttonColors(containerColor = HackRed),
            ) {
                Icon(Icons.Filled.VolumeOff, contentDescription = null, modifier = Modifier.size(28.dp))
                Spacer(Modifier.width(12.dp))
                Text(
                    "STOP ALARM",
                    fontSize = 20.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                )
            }
        }
    }
}
