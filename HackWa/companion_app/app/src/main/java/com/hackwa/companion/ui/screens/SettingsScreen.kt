package com.hackwa.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.hackwa.companion.ui.theme.*
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    timezones: List<Pair<String, String>>,
    onSetTimezone: (String) -> Unit,
    onSendRaw: (String) -> Unit,
    onFindWatch: () -> Unit,
    onSetScreenTimeout: (Int) -> Unit,
    onSetBrightness: (Int) -> Unit,
    onBack: () -> Unit,
) {
    var selectedTz by remember { mutableStateOf("") }
    var rawCmd by remember { mutableStateOf("") }
    var rawSent by remember { mutableStateOf(false) }
    var timeoutSeconds by remember { mutableFloatStateOf(30f) }
    var brightnessLevel by remember { mutableIntStateOf(1) }  // 0=Dim,1=Med,2=Bright

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        "Settings",
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Filled.ArrowBack, "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = HackSurface,
                    titleContentColor = HackDim,
                    navigationIconContentColor = HackText,
                ),
            )
        },
        containerColor = HackDark,
    ) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            // ── Timezone ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Language, null, tint = HackCyan)
                        Spacer(Modifier.width(8.dp))
                        Text("Timezone", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(12.dp))

                    timezones.forEach { (posix, label) ->
                        val isSelected = selectedTz == posix
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            RadioButton(
                                selected = isSelected,
                                onClick = { selectedTz = posix },
                                colors = RadioButtonDefaults.colors(
                                    selectedColor = HackGreen,
                                    unselectedColor = HackDim,
                                ),
                            )
                            Text(label, color = if (isSelected) HackGreen else HackText)
                            Spacer(Modifier.weight(1f))
                            Text(
                                posix.take(16),
                                style = MaterialTheme.typography.bodySmall,
                                color = HackDim,
                                fontFamily = FontFamily.Monospace,
                            )
                        }
                    }

                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = {
                            if (selectedTz.isNotBlank()) onSetTimezone(selectedTz)
                        },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = HackGreen),
                        enabled = selectedTz.isNotBlank(),
                    ) {
                        Text("Apply Timezone", color = HackDark, fontWeight = FontWeight.Bold)
                    }
                }
            }

            // ── Find My Watch ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Watch, null, tint = HackPurple)
                        Spacer(Modifier.width(8.dp))
                        Text("Find My Watch", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Makes the watch OLED blink so you can locate it.",
                        style = MaterialTheme.typography.bodySmall,
                        color = HackDim,
                    )
                    Spacer(Modifier.height(12.dp))
                    Button(
                        onClick = onFindWatch,
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = HackPurple),
                    ) {
                        Icon(Icons.Filled.FlashlightOn, null, tint = HackDark)
                        Spacer(Modifier.width(8.dp))
                        Text("Find Watch", color = HackDark, fontWeight = FontWeight.Bold)
                    }
                }
            }

            // ── Screen Timeout ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Timer, null, tint = HackBlue)
                        Spacer(Modifier.width(8.dp))
                        Text("Screen Timeout", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Adjust how long the OLED stays on before sleeping.",
                        style = MaterialTheme.typography.bodySmall,
                        color = HackDim,
                    )
                    Spacer(Modifier.height(12.dp))
                    Text(
                        "${timeoutSeconds.roundToInt()} seconds",
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        color = HackBlue,
                        fontSize = 18.sp,
                    )
                    Slider(
                        value = timeoutSeconds,
                        onValueChange = { timeoutSeconds = it },
                        valueRange = 5f..300f,
                        steps = 58,
                        modifier = Modifier.fillMaxWidth(),
                        colors = SliderDefaults.colors(
                            thumbColor = HackBlue,
                            activeTrackColor = HackBlue,
                            inactiveTrackColor = HackSurface,
                        ),
                    )
                    Spacer(Modifier.height(4.dp))
                    Button(
                        onClick = { onSetScreenTimeout(timeoutSeconds.roundToInt()) },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = HackBlue),
                    ) {
                        Text("Apply Timeout", color = HackDark, fontWeight = FontWeight.Bold)
                    }
                }
            }

            // ── Brightness ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Brightness6, null, tint = HackCyan)
                        Spacer(Modifier.width(8.dp))
                        Text("Brightness", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Lower brightness saves OLED power and extends battery life.",
                        style = MaterialTheme.typography.bodySmall,
                        color = HackDim,
                    )
                    Spacer(Modifier.height(12.dp))
                    val labels = listOf("Dim", "Medium", "Bright")
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        labels.forEachIndexed { idx, label ->
                            val isSelected = brightnessLevel == idx
                            Button(
                                onClick = {
                                    brightnessLevel = idx
                                    onSetBrightness(idx)
                                },
                                modifier = Modifier.weight(1f),
                                shape = RoundedCornerShape(12.dp),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = if (isSelected) HackCyan else HackSurface,
                                ),
                            ) {
                                Text(
                                    label,
                                    color = if (isSelected) HackDark else HackDim,
                                    fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                    fontSize = 13.sp,
                                )
                            }
                        }
                    }
                }
            }

            // ── Raw AT Command ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Terminal, null, tint = HackOrange)
                        Spacer(Modifier.width(8.dp))
                        Text("Raw AT Command", fontWeight = FontWeight.SemiBold, color = HackText)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "Send any AT command directly to the watch.",
                        style = MaterialTheme.typography.bodySmall,
                        color = HackDim,
                    )
                    Spacer(Modifier.height(12.dp))
                    OutlinedTextField(
                        value = rawCmd,
                        onValueChange = { rawCmd = it; rawSent = false },
                        label = { Text("e.g. AT+DT=20260221153000") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = HackOrange,
                            cursorColor = HackOrange,
                        ),
                        textStyle = MaterialTheme.typography.bodyMedium.copy(
                            fontFamily = FontFamily.Monospace,
                        ),
                    )
                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = {
                            if (rawCmd.isNotBlank()) {
                                onSendRaw(rawCmd.trim())
                                rawSent = true
                            }
                        },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = HackOrange),
                        enabled = rawCmd.isNotBlank(),
                    ) {
                        Icon(Icons.Filled.Send, null, tint = HackDark)
                        Spacer(Modifier.width(8.dp))
                        Text("Send", color = HackDark, fontWeight = FontWeight.Bold)
                    }
                    if (rawSent) {
                        Spacer(Modifier.height(4.dp))
                        Text("✓ Sent!", color = HackGreen)
                    }
                }
            }

            // ── AT Command Reference ──
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("AT Command Reference", fontWeight = FontWeight.SemiBold, color = HackDim)
                    Spacer(Modifier.height(8.dp))
                    val cmds = listOf(
                        "AT+DT=YYYYMMDDHHmmss" to "Set date/time",
                        "AT+NT=title|body" to "Push notification",
                        "AT+PS=slot|label|pass" to "Store password",
                        "AT+PD=slot" to "Delete password",
                        "AT+TZ=POSIX_TZ" to "Set timezone",
                        "AT+FW" to "Find my watch (blink OLED)",
                        "AT+ST=seconds" to "Screen timeout (5-300)",
                        "AT+BR=level" to "Brightness (0=dim,1=med,2=bright)",
                        "AT+FP" to "Find my phone (alarm)",
                    )
                    cmds.forEach { (cmd, desc) ->
                        Row(modifier = Modifier.padding(vertical = 2.dp)) {
                            Text(
                                cmd,
                                fontFamily = FontFamily.Monospace,
                                color = HackGreen,
                                style = MaterialTheme.typography.bodySmall,
                                modifier = Modifier.weight(1f),
                            )
                            Text(
                                desc,
                                color = HackDim,
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                }
            }
        }
    }
}
