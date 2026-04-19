package com.hackwa.companion

import android.Manifest
import android.bluetooth.le.ScanResult
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.hackwa.companion.ble.BleConnectionService
import com.hackwa.companion.ble.BleManager
import com.hackwa.companion.ui.screens.*
import com.hackwa.companion.ui.theme.HackDark
import com.hackwa.companion.ui.theme.HackWaTheme

class MainActivity : ComponentActivity() {

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { grants ->
        // Once permissions are granted, start the BLE service
        if (grants.values.all { it }) {
            startBleService()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestBlePermissions()
        startBleService()

        setContent {
            HackWaTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = HackDark,
                ) {
                    HackWaApp()
                }
            }
        }
    }

    private fun startBleService() {
        val intent = Intent(this, BleConnectionService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }

    private fun requestBlePermissions() {
        val perms = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            perms.add(Manifest.permission.BLUETOOTH_SCAN)
            perms.add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            perms.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        perms.add(Manifest.permission.ACCESS_FINE_LOCATION)

        val needed = perms.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isNotEmpty()) {
            permissionLauncher.launch(needed.toTypedArray())
        }
    }
}

@Composable
fun HackWaApp(vm: MainViewModel = viewModel()) {
    val navController = rememberNavController()
    val connectionState by vm.ble.connectionState.collectAsState()
    val scanResults by vm.ble.scanResults.collectAsState()
    val deviceName by vm.ble.deviceName.collectAsState()
    val logs by vm.ble.logMessages.collectAsState()
    val passwords by vm.passwords.collectAsState()
    val findPhoneRinging by vm.ble.findPhoneRinging.collectAsState()

    // Auto-start connection if we have a saved device
    LaunchedEffect(Unit) {
        if (vm.ble.hasSavedDevice() && !vm.ble.isConnected()) {
            vm.ble.startAutoConnect()
        }
    }

    // Auto-navigate on connect/disconnect
    LaunchedEffect(connectionState) {
        when (connectionState) {
            is BleManager.ConnectionState.Connected -> {
                navController.navigate("dashboard") {
                    popUpTo("scan") { inclusive = true }
                }
            }
            is BleManager.ConnectionState.Disconnected -> {
                // Only go back to scan if user manually disconnected (no auto-reconnect)
                if (!vm.ble.autoReconnectEnabled) {
                    if (navController.currentDestination?.route != "scan") {
                        navController.navigate("scan") {
                            popUpTo(0) { inclusive = true }
                        }
                    }
                }
            }
            else -> {}
        }
    }

    Box(Modifier.fillMaxSize()) {
    NavHost(navController, startDestination = "scan") {

        composable("scan") {
            ScanScreen(
                connectionState = connectionState,
                scanResults = scanResults,
                onStartScan = { vm.ble.startScan() },
                onStopScan = { vm.ble.stopScan() },
                onConnect = { result: ScanResult -> vm.ble.connect(result.device) },
            )
        }

        composable("dashboard") {
            DashboardScreen(
                deviceName = deviceName,
                onSyncTime = { vm.ble.syncTime() },
                onDisconnect = { vm.ble.disconnect() },
                onNavigate = { route -> navController.navigate(route) },
                onFindWatch = { vm.ble.sendFindWatch() },
                logs = logs,
            )
        }

        composable("passwords") {
            PasswordScreen(
                passwords = passwords,
                onStore = { slot, label, pass ->
                    vm.ble.storePassword(slot, label, pass)
                    vm.addPasswordLocal(slot, label, pass)
                },
                onDelete = { slot ->
                    vm.ble.deletePassword(slot)
                    vm.removePasswordLocal(slot)
                },
                onBack = { navController.popBackStack() },
            )
        }

        composable("notify") {
            NotifyScreen(
                onSend = { title, body -> vm.ble.sendNotification(title, body) },
                onBack = { navController.popBackStack() },
            )
        }

        composable("settings") {
            SettingsScreen(
                timezones = vm.timezones,
                onSetTimezone = { vm.ble.setTimezone(it) },
                onSendRaw = { vm.ble.sendRaw(it) },
                onFindWatch = { vm.ble.sendFindWatch() },
                onSetScreenTimeout = { vm.ble.setScreenTimeout(it) },
                onSetBrightness = { vm.ble.setBrightness(it) },
                onBack = { navController.popBackStack() },
            )
        }
    }

    // Full-screen alarm overlay when watch triggers Find My Phone
    if (findPhoneRinging) {
        FindPhoneAlarmOverlay(
            onStop = { vm.ble.stopFindPhoneAlarm() }
        )
    }
    } // end Box
}
