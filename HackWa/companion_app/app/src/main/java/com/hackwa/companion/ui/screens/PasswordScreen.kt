package com.hackwa.companion.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.hackwa.companion.MainViewModel
import com.hackwa.companion.ui.theme.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PasswordScreen(
    passwords: List<MainViewModel.PwEntry>,
    onStore: (slot: Int, label: String, password: String) -> Unit,
    onDelete: (slot: Int) -> Unit,
    onBack: () -> Unit,
) {
    var showDialog by remember { mutableStateOf(false) }
    var editSlot by remember { mutableIntStateOf(-1) }
    var editLabel by remember { mutableStateOf("") }
    var editPass by remember { mutableStateOf("") }
    var showPass by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        "Password Manager",
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
                    titleContentColor = HackGreen,
                    navigationIconContentColor = HackText,
                ),
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                onClick = {
                    // Find next free slot
                    val usedSlots = passwords.map { it.slot }.toSet()
                    val freeSlot = (0..9).firstOrNull { it !in usedSlots }
                    if (freeSlot != null) {
                        editSlot = freeSlot
                        editLabel = ""
                        editPass = ""
                        showDialog = true
                    }
                },
                containerColor = HackGreen,
                contentColor = HackDark,
            ) {
                Icon(Icons.Filled.Add, "Add Password")
            }
        },
        containerColor = HackDark,
    ) { pad ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item { Spacer(Modifier.height(4.dp)) }

            // Slot usage indicator
            item {
                Card(
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = HackCard),
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Slots", color = HackDim, fontWeight = FontWeight.SemiBold)
                        Spacer(Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceEvenly,
                        ) {
                            for (i in 0..9) {
                                val used = passwords.any { it.slot == i }
                                Box(
                                    modifier = Modifier
                                        .size(28.dp)
                                        .background(
                                            if (used) HackGreen else HackSurface,
                                            RoundedCornerShape(6.dp)
                                        ),
                                    contentAlignment = Alignment.Center,
                                ) {
                                    Text(
                                        "$i",
                                        fontFamily = FontFamily.Monospace,
                                        color = if (used) HackDark else HackDim,
                                        fontWeight = FontWeight.Bold,
                                    )
                                }
                            }
                        }
                    }
                }
            }

            if (passwords.isEmpty()) {
                item {
                    Spacer(Modifier.height(32.dp))
                    Text(
                        "No passwords stored.\nTap + to add one.",
                        color = HackDim,
                        modifier = Modifier.fillMaxWidth(),
                        textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                    )
                }
            }

            itemsIndexed(passwords) { _, entry ->
                var revealPw by remember { mutableStateOf(false) }
                Card(
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = HackCard),
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        // Slot badge
                        Box(
                            modifier = Modifier
                                .size(36.dp)
                                .background(HackOrange, RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center,
                        ) {
                            Text(
                                "${entry.slot}",
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace,
                                color = HackDark,
                            )
                        }
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text(entry.label, fontWeight = FontWeight.SemiBold, color = HackText)
                            Text(
                                if (revealPw) entry.password else "••••••••",
                                fontFamily = FontFamily.Monospace,
                                color = if (revealPw) HackGreen else HackDim,
                            )
                        }
                        IconButton(onClick = { revealPw = !revealPw }) {
                            Icon(
                                if (revealPw) Icons.Filled.VisibilityOff else Icons.Filled.Visibility,
                                null,
                                tint = HackDim,
                            )
                        }
                        IconButton(onClick = { onDelete(entry.slot) }) {
                            Icon(Icons.Filled.Delete, null, tint = HackRed)
                        }
                    }
                }
            }

            item { Spacer(Modifier.height(80.dp)) }
        }
    }

    // ── Add/Edit dialog ──
    if (showDialog) {
        AlertDialog(
            onDismissRequest = { showDialog = false },
            containerColor = HackSurface,
            title = {
                Text(
                    "Add Password (Slot $editSlot)",
                    color = HackGreen,
                    fontFamily = FontFamily.Monospace,
                )
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedTextField(
                        value = editLabel,
                        onValueChange = { if (it.length <= 20) editLabel = it },
                        label = { Text("Label (max 20 chars)") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = HackGreen,
                            cursorColor = HackGreen,
                        ),
                    )
                    OutlinedTextField(
                        value = editPass,
                        onValueChange = { if (it.length <= 32) editPass = it },
                        label = { Text("Password (max 32 chars)") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        visualTransformation = if (showPass) VisualTransformation.None
                        else PasswordVisualTransformation(),
                        trailingIcon = {
                            IconButton(onClick = { showPass = !showPass }) {
                                Icon(
                                    if (showPass) Icons.Filled.VisibilityOff
                                    else Icons.Filled.Visibility,
                                    null,
                                )
                            }
                        },
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = HackGreen,
                            cursorColor = HackGreen,
                        ),
                    )
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        if (editLabel.isNotBlank() && editPass.isNotBlank()) {
                            onStore(editSlot, editLabel.trim(), editPass)
                            showDialog = false
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = HackGreen),
                ) {
                    Text("Save to Watch", color = HackDark, fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDialog = false }) {
                    Text("Cancel", color = HackDim)
                }
            },
        )
    }
}
