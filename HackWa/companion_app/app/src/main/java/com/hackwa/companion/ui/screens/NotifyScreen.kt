package com.hackwa.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.hackwa.companion.ui.theme.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NotifyScreen(
    onSend: (title: String, body: String) -> Unit,
    onBack: () -> Unit,
) {
    var title by remember { mutableStateOf("") }
    var body by remember { mutableStateOf("") }
    var sent by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        "Send Notification",
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
                    titleContentColor = HackCyan,
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
            // Quick presets
            Text("Quick Presets", color = HackDim, fontWeight = FontWeight.SemiBold)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AssistChip(
                    onClick = {
                        title = "Reminder"
                        body = "Don't forget!"
                    },
                    label = { Text("Reminder") },
                    leadingIcon = { Icon(Icons.Filled.Alarm, null, Modifier.size(16.dp)) },
                    colors = AssistChipDefaults.assistChipColors(
                        containerColor = HackCard,
                        labelColor = HackText,
                        leadingIconContentColor = HackOrange,
                    ),
                )
                AssistChip(
                    onClick = {
                        title = "Alert"
                        body = "Check your phone"
                    },
                    label = { Text("Alert") },
                    leadingIcon = { Icon(Icons.Filled.Warning, null, Modifier.size(16.dp)) },
                    colors = AssistChipDefaults.assistChipColors(
                        containerColor = HackCard,
                        labelColor = HackText,
                        leadingIconContentColor = HackRed,
                    ),
                )
                AssistChip(
                    onClick = {
                        title = "Test"
                        body = "Hello from HackWa app!"
                    },
                    label = { Text("Test") },
                    leadingIcon = { Icon(Icons.Filled.Science, null, Modifier.size(16.dp)) },
                    colors = AssistChipDefaults.assistChipColors(
                        containerColor = HackCard,
                        labelColor = HackText,
                        leadingIconContentColor = HackCyan,
                    ),
                )
            }

            // Custom notification
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = HackCard),
            ) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    OutlinedTextField(
                        value = title,
                        onValueChange = { if (it.length <= 30) title = it },
                        label = { Text("Title") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = HackCyan,
                            cursorColor = HackCyan,
                        ),
                    )
                    OutlinedTextField(
                        value = body,
                        onValueChange = { if (it.length <= 60) body = it },
                        label = { Text("Body") },
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(min = 80.dp),
                        maxLines = 3,
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = HackCyan,
                            cursorColor = HackCyan,
                        ),
                    )

                    Button(
                        onClick = {
                            if (title.isNotBlank() && body.isNotBlank()) {
                                onSend(title.trim(), body.trim())
                                sent = true
                            }
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp),
                        shape = RoundedCornerShape(12.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = HackCyan),
                        enabled = title.isNotBlank() && body.isNotBlank(),
                    ) {
                        Icon(Icons.Filled.Send, null)
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Send to Watch",
                            fontWeight = FontWeight.Bold,
                            color = HackDark,
                        )
                    }

                    if (sent) {
                        Text(
                            "✓ Notification sent!",
                            color = HackGreen,
                        )
                    }
                }
            }
        }
    }
}
