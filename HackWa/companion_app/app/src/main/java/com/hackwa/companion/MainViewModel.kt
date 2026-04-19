package com.hackwa.companion

import android.app.Application
import android.bluetooth.le.ScanResult
import androidx.lifecycle.AndroidViewModel
import com.hackwa.companion.ble.BleConnectionService
import com.hackwa.companion.ble.BleManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class MainViewModel(app: Application) : AndroidViewModel(app) {

    // Use the service singleton so BLE survives in background
    val ble: BleManager = BleConnectionService.bleManager
        ?: BleManager(app.applicationContext).also { BleConnectionService.bleManager = it }

    // Password editor state
    data class PwEntry(val slot: Int, val label: String, val password: String)

    private val _passwords = MutableStateFlow<List<PwEntry>>(emptyList())
    val passwords: StateFlow<List<PwEntry>> = _passwords

    fun addPasswordLocal(slot: Int, label: String, password: String) {
        val list = _passwords.value.toMutableList()
        list.removeAll { it.slot == slot }
        list.add(PwEntry(slot, label, password))
        list.sortBy { it.slot }
        _passwords.value = list
    }

    fun removePasswordLocal(slot: Int) {
        _passwords.value = _passwords.value.filter { it.slot != slot }
    }

    // Timezone options
    val timezones = listOf(
        "UTC0"                     to "UTC",
        "EST5EDT,M3.2.0,M11.1.0"  to "US Eastern",
        "CST6CDT,M3.2.0,M11.1.0"  to "US Central",
        "MST7MDT,M3.2.0,M11.1.0"  to "US Mountain",
        "PST8PDT,M3.2.0,M11.1.0"  to "US Pacific",
        "GMT0BST,M3.5.0/1,M10.5.0" to "UK",
        "CET-1CEST,M3.5.0,M10.5.0/3" to "Europe Central",
        "IST-5:30"                 to "India",
        "CST-8"                    to "China/Singapore",
        "JST-9"                    to "Japan",
        "AEST-10AEDT,M10.1.0,M4.1.0/3" to "Australia East",
    )

    override fun onCleared() {
        // Don't disconnect — service keeps connection alive in background
        super.onCleared()
    }
}
