package com.hackwa.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.content.SharedPreferences
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.text.SimpleDateFormat
import java.util.*

/**
 * BLE Manager — handles scanning, connecting, auto-reconnecting,
 * and sending AT commands to the HackWa watch over NUS.
 *
 * Key behaviours:
 *  - Saves last-connected MAC so it can reconnect automatically.
 *  - On disconnect, immediately starts a low-power background scan.
 *  - When the saved device appears, auto-connects without user action.
 *  - Syncs time on every (re)connect.
 */
@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    companion object {
        private const val TAG = "HackWaBLE"
        private const val PREFS_NAME = "hackwa_ble"
        private const val KEY_SAVED_MAC = "saved_mac"
        private const val KEY_SAVED_NAME = "saved_name"

        val NUS_SERVICE_UUID: UUID  = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val NUS_RX_CHAR_UUID: UUID  = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val NUS_TX_CHAR_UUID: UUID  = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        private const val RECONNECT_DELAY_MS = 3000L
        private const val SCAN_WINDOW_MS     = 15000L
        private const val SCAN_PAUSE_MS      = 10000L
    }

    // ── Persisted prefs ────────────────────────────────────
    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    private fun savePairedDevice(mac: String, name: String) {
        prefs.edit().putString(KEY_SAVED_MAC, mac).putString(KEY_SAVED_NAME, name).apply()
    }
    fun getSavedMac(): String?  = prefs.getString(KEY_SAVED_MAC, null)
    fun getSavedName(): String? = prefs.getString(KEY_SAVED_NAME, null)
    fun clearSavedDevice() {
        prefs.edit().remove(KEY_SAVED_MAC).remove(KEY_SAVED_NAME).apply()
    }

    // ── State ──────────────────────────────────────────────
    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Scanning : ConnectionState()
        object Connecting : ConnectionState()
        object Connected : ConnectionState()
    }

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState

    private val _deviceName = MutableStateFlow(getSavedName() ?: "")
    val deviceName: StateFlow<String> = _deviceName

    private val _logMessages = MutableStateFlow<List<String>>(emptyList())
    val logMessages: StateFlow<List<String>> = _logMessages

    private val _scanResults = MutableStateFlow<List<ScanResult>>(emptyList())
    val scanResults: StateFlow<List<ScanResult>> = _scanResults

    // ── Find My Phone alarm state ──
    private val _findPhoneRinging = MutableStateFlow(false)
    val findPhoneRinging: StateFlow<Boolean> = _findPhoneRinging

    private var alarmPlayer: MediaPlayer? = null
    private var savedVolume: Int = -1
    private val audioManager: AudioManager by lazy {
        context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bm.adapter
    }
    private val scanner: BluetoothLeScanner? get() = bluetoothAdapter?.bluetoothLeScanner

    private val handler = Handler(Looper.getMainLooper())

    /** When true, auto-reconnect is active (user hasn't manually disconnected) */
    var autoReconnectEnabled = true
        private set

    private var autoScanning = false

    // ── Logging ────────────────────────────────────────────
    private fun log(msg: String) {
        Log.d(TAG, msg)
        val ts = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        val entry = "[$ts] $msg"
        _logMessages.value = (_logMessages.value + entry).takeLast(100)
    }

    // ══════════════════════════════════════════════════════════
    //  Manual Scan (from Scan screen — full power, shows results)
    // ══════════════════════════════════════════════════════════
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: return
            val current = _scanResults.value.toMutableList()
            if (current.none { it.device.address == result.device.address }) {
                current.add(result)
                _scanResults.value = current
                log("Found: $name (${result.device.address})")
            }
            // Auto-connect to HackWa device if no saved device yet
            if (getSavedMac() == null && name.startsWith("HackWa", ignoreCase = true)) {
                log("Auto-connecting to $name...")
                stopScan()
                connect(result.device)
            }
        }
        override fun onScanFailed(errorCode: Int) {
            log("Scan failed: error $errorCode")
            _connectionState.value = ConnectionState.Disconnected
        }
    }

    fun startScan() {
        stopAutoScan()
        _scanResults.value = emptyList()
        _connectionState.value = ConnectionState.Scanning
        log("Scanning for HackWa devices...")

        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(NUS_SERVICE_UUID)).build(),
            ScanFilter.Builder().setDeviceName("HackWa").build(),
            ScanFilter.Builder().setDeviceName("HackWa-KB").build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner?.startScan(filters, settings, scanCallback)

        handler.postDelayed({
            if (_scanResults.value.isEmpty() && _connectionState.value == ConnectionState.Scanning) {
                scanner?.stopScan(scanCallback)
                scanner?.startScan(scanCallback)
                log("Widened scan (no filter)...")
            }
        }, 3000)
    }

    fun stopScan() {
        try { scanner?.stopScan(scanCallback) } catch (_: Exception) {}
        if (_connectionState.value == ConnectionState.Scanning) {
            _connectionState.value = ConnectionState.Disconnected
        }
    }

    // ══════════════════════════════════════════════════════════
    //  Auto-Scan (background, low power, targets saved MAC)
    // ══════════════════════════════════════════════════════════
    private val autoScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val mac = result.device.address
            val savedMac = getSavedMac()
            if (mac == savedMac) {
                log("Auto-scan: found saved device — connecting...")
                stopAutoScan()
                connectDevice(result.device)
            }
        }
        override fun onScanFailed(errorCode: Int) {
            Log.w(TAG, "Auto-scan failed: $errorCode")
            autoScanning = false
            scheduleAutoScanCycle()
        }
    }

    /** Start the background auto-reconnect scan cycle */
    fun startAutoConnect() {
        autoReconnectEnabled = true
        val savedMac = getSavedMac()
        if (savedMac == null) {
            log("No saved device — waiting for manual scan")
            return
        }
        if (_connectionState.value == ConnectionState.Connected ||
            _connectionState.value == ConnectionState.Connecting) return

        log("Auto-connect: looking for ${getSavedName()} ...")
        _connectionState.value = ConnectionState.Scanning
        startAutoScanWindow()
    }

    private fun startAutoScanWindow() {
        if (!autoReconnectEnabled) return
        if (_connectionState.value == ConnectionState.Connected) return

        autoScanning = true
        val savedMac = getSavedMac() ?: return

        val filters = listOf(
            ScanFilter.Builder().setDeviceAddress(savedMac).build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
            .build()

        try {
            scanner?.startScan(filters, settings, autoScanCallback)
        } catch (e: Exception) {
            Log.w(TAG, "Auto-scan start error: ${e.message}")
        }

        handler.postDelayed(autoScanStopRunnable, SCAN_WINDOW_MS)
    }

    private val autoScanStopRunnable = Runnable {
        stopAutoScanImmediate()
        scheduleAutoScanCycle()
    }

    private fun scheduleAutoScanCycle() {
        if (!autoReconnectEnabled) return
        if (_connectionState.value == ConnectionState.Connected) return
        handler.postDelayed({ startAutoScanWindow() }, SCAN_PAUSE_MS)
    }

    private fun stopAutoScanImmediate() {
        try { scanner?.stopScan(autoScanCallback) } catch (_: Exception) {}
        autoScanning = false
    }

    fun stopAutoScan() {
        handler.removeCallbacksAndMessages(null)
        stopAutoScanImmediate()
    }

    // ══════════════════════════════════════════════════════════
    //  Connect / Disconnect
    // ══════════════════════════════════════════════════════════
    fun connect(device: BluetoothDevice) {
        stopScan()
        stopAutoScan()
        autoReconnectEnabled = true
        connectDevice(device)
    }

    private fun connectDevice(device: BluetoothDevice) {
        bluetoothGatt?.let { it.disconnect(); it.close() }
        bluetoothGatt = null
        rxCharacteristic = null
        txCharacteristic = null

        _connectionState.value = ConnectionState.Connecting
        _deviceName.value = device.name ?: device.address
        log("Connecting to ${device.name ?: device.address}...")
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    /** User-initiated disconnect — stops auto-reconnect */
    fun disconnect() {
        autoReconnectEnabled = false
        stopAutoScan()
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        rxCharacteristic = null
        txCharacteristic = null
        _connectionState.value = ConnectionState.Disconnected
        log("Disconnected (manual)")
    }

    /** Forget the saved device entirely */
    fun forgetDevice() {
        disconnect()
        clearSavedDevice()
        _deviceName.value = ""
        log("Forgot paired device")
    }

    // ══════════════════════════════════════════════════════════
    //  GATT Callback
    // ══════════════════════════════════════════════════════════
    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    log("Connected! Discovering services...")
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    log("Disconnected (status=$status)")
                    _connectionState.value = ConnectionState.Disconnected
                    rxCharacteristic = null
                    txCharacteristic = null
                    gatt.close()
                    if (bluetoothGatt == gatt) bluetoothGatt = null

                    if (autoReconnectEnabled && getSavedMac() != null) {
                        log("Will auto-reconnect in ${RECONNECT_DELAY_MS / 1000}s...")
                        handler.postDelayed({ startAutoConnect() }, RECONNECT_DELAY_MS)
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                log("Service discovery failed: $status")
                return
            }

            val nusService = gatt.getService(NUS_SERVICE_UUID)
            if (nusService == null) {
                log("NUS service not found! Is watch in Watch mode?")
                return
            }

            rxCharacteristic = nusService.getCharacteristic(NUS_RX_CHAR_UUID)
            txCharacteristic = nusService.getCharacteristic(NUS_TX_CHAR_UUID)

            if (rxCharacteristic == null || txCharacteristic == null) {
                log("NUS characteristics not found!")
                return
            }

            gatt.setCharacteristicNotification(txCharacteristic, true)
            val desc = txCharacteristic!!.getDescriptor(CCCD_UUID)
            if (desc != null) {
                desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(desc)
            } else {
                log("No CCCD descriptor, syncing time directly")
                syncTime()
            }

            _connectionState.value = ConnectionState.Connected
            val device = gatt.device
            savePairedDevice(device.address, device.name ?: device.address)
            _deviceName.value = device.name ?: device.address
            log("Ready! Connected to ${device.name ?: device.address}")
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "CCCD notification enabled")
                handler.postDelayed({ syncTime() }, 300)
            } else {
                log("CCCD write failed: $status")
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val data = characteristic.value ?: return
            if (characteristic.uuid == NUS_TX_CHAR_UUID) {
                val text = String(data, Charsets.UTF_8).trim()
                log("Watch -> $text")
                if (text == "AT+FP") {
                    triggerFindPhoneAlarm()
                }
                if (text.startsWith("AT+MD=")) {
                    val modeStr = text.removePrefix("AT+MD=")
                    if (modeStr == "HID") {
                        log("Watch switching to HID mode – disabling auto-reconnect")
                        autoReconnectEnabled = false
                        stopAutoScan()
                    }
                }
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Write OK")
            } else {
                log("Write failed: $status")
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  AT Commands
    // ══════════════════════════════════════════════════════════
    private fun sendCommand(cmd: String): Boolean {
        val rx = rxCharacteristic ?: return false
        val gatt = bluetoothGatt ?: return false
        rx.value = cmd.toByteArray(Charsets.UTF_8)
        rx.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val ok = gatt.writeCharacteristic(rx)
        log("App -> $cmd ${if (ok) "ok" else "FAIL"}")
        return ok
    }

    fun syncTime() {
        val sdf = SimpleDateFormat("yyyyMMddHHmmss", Locale.US)
        sendCommand("AT+DT=${sdf.format(Date())}")
    }
    fun setTimezone(tz: String) { sendCommand("AT+TZ=$tz") }
    fun sendNotification(title: String, body: String) { sendCommand("AT+NT=$title|$body") }
    fun storePassword(slot: Int, label: String, password: String) { sendCommand("AT+PS=$slot|$label|$password") }
    fun deletePassword(slot: Int) { sendCommand("AT+PD=$slot") }
    fun sendRaw(cmd: String) { sendCommand(cmd) }
    fun sendFindWatch() { sendCommand("AT+FW") }
    fun setScreenTimeout(seconds: Int) { sendCommand("AT+ST=$seconds") }
    fun setBrightness(level: Int)       { sendCommand("AT+BR=$level") }

    // ══════════════════════════════════════════════════════════
    //  Find My Phone Alarm
    // ══════════════════════════════════════════════════════════
    private fun triggerFindPhoneAlarm() {
        if (_findPhoneRinging.value) return
        log("FIND MY PHONE — ringing!")
        _findPhoneRinging.value = true
        try {
            savedVolume = audioManager.getStreamVolume(AudioManager.STREAM_ALARM)
            val maxVol = audioManager.getStreamMaxVolume(AudioManager.STREAM_ALARM)
            audioManager.setStreamVolume(AudioManager.STREAM_ALARM, maxVol, 0)
            val alarmUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
                ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
                ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
            alarmPlayer = MediaPlayer().apply {
                setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                setDataSource(context, alarmUri)
                isLooping = true
                prepare()
                start()
            }
        } catch (e: Exception) {
            log("Alarm error: ${e.message}")
            _findPhoneRinging.value = false
        }
    }

    fun stopFindPhoneAlarm() {
        alarmPlayer?.let { if (it.isPlaying) it.stop(); it.release() }
        alarmPlayer = null
        if (savedVolume >= 0) {
            audioManager.setStreamVolume(AudioManager.STREAM_ALARM, savedVolume, 0)
            savedVolume = -1
        }
        _findPhoneRinging.value = false
        log("Find My Phone alarm stopped")
    }

    fun isConnected(): Boolean = _connectionState.value == ConnectionState.Connected
    fun hasSavedDevice(): Boolean = getSavedMac() != null
}
