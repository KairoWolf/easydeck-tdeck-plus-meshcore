package com.easydeck.app

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.UUID
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * Bluetooth LE link to a MeshCore companion (EasyDeck or stock firmware) over the
 * Nordic UART Service: app writes frames to RX, device notifies frames on TX.
 * Writes are serialised (Android allows one GATT operation in flight).
 */
@SuppressLint("MissingPermission")   // permissions are requested by MainActivity before use
class BleLink(private val ctx: Context) {
    companion object {
        val SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val RX: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")   // app -> device
        val TX: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")   // device -> app
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val TAG = "EasyDeckBle"
    }

    data class Found(val address: String, val name: String, val rssi: Int)
    enum class State { IDLE, SCANNING, CONNECTING, READY, DISCONNECTED }

    private val adapter: BluetoothAdapter? = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private val writeQueue = ConcurrentLinkedQueue<ByteArray>()
    @Volatile private var writing = false

    val state = MutableStateFlow(State.IDLE)
    val found = MutableStateFlow<List<Found>>(emptyList())
    /** Every frame the device sends. */
    val frames = Channel<ByteArray>(Channel.UNLIMITED)
    var deviceName: String = ""; private set

    // ---------------------------------------------------------------- scan
    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, r: ScanResult) {
            val name = r.scanRecord?.deviceName ?: r.device.name ?: return
            val list = found.value.filter { it.address != r.device.address } + Found(r.device.address, name, r.rssi)
            found.value = list.sortedByDescending { it.rssi }
        }
    }

    fun hasPermission(): Boolean =
        ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) == android.content.pm.PackageManager.PERMISSION_GRANTED &&
        ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN) == android.content.pm.PackageManager.PERMISSION_GRANTED

    fun startScan() {
        if (!hasPermission()) return
        val sc = adapter?.bluetoothLeScanner ?: return
        found.value = emptyList()
        state.value = State.SCANNING
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        sc.startScan(listOf(filter), settings, scanCb)
    }

    fun stopScan() {
        if (hasPermission()) try { adapter?.bluetoothLeScanner?.stopScan(scanCb) } catch (e: Exception) { }
        if (state.value == State.SCANNING) state.value = State.IDLE
    }

    // ---------------------------------------------------------------- connect
    fun connect(address: String, name: String) {
        if (!hasPermission()) return
        stopScan()
        disconnect()
        deviceName = name
        state.value = State.CONNECTING
        val dev = adapter?.getRemoteDevice(address) ?: return
        gatt = dev.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        try { gatt?.disconnect(); gatt?.close() } catch (e: SecurityException) { }
        gatt = null
        rxChar = null
        writeQueue.clear()
        writing = false
        if (state.value != State.IDLE) state.value = State.DISCONNECTED
    }

    fun send(frame: ByteArray) {
        writeQueue.add(frame)
        pump()
    }

    private fun pump() {
        val g = gatt ?: return
        val c = rxChar ?: return
        if (writing) return
        val f = writeQueue.poll() ?: return
        writing = true
        val rc = g.writeCharacteristic(c, f, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        if (rc != BluetoothStatusCodes.SUCCESS) { writing = false; Log.w(TAG, "write failed $rc") }
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestMtu(512)   // one frame (<=176 bytes) per packet
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                rxChar = null
                writing = false
                state.value = State.DISCONNECTED
            }
        }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) { g.discoverServices() }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(SERVICE) ?: run { Log.w(TAG, "no NUS service"); g.disconnect(); return }
            rxChar = svc.getCharacteristic(RX)
            val tx = svc.getCharacteristic(TX)
            g.setCharacteristicNotification(tx, true)
            val d = tx.getDescriptor(CCCD)
            g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            state.value = State.READY   // bonding/PIN prompt is handled by Android here if needed
            pump()
        }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            writing = false
            pump()
        }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            frames.trySend(value)
        }
    }
}
