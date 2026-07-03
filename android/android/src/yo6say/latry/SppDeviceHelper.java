package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothUuid;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.ParcelUuid;
import android.util.Log;

import org.json.JSONObject;

import java.lang.reflect.Method;
import java.util.Set;
import java.util.UUID;

/**
 * Helper to find the first connected Classic Bluetooth SPP device for
 * automatic PTT learning mode.
 *
 * Uses reflection to call BluetoothDevice.isConnected() (hidden API) to
 * check ACL connection state, and checks for the SPP UUID to filter out
 * non-SPP devices (headsets, speakers, etc.).
 *
 * If getUuids() returns null (cache empty), the device is included anyway
 * and SppPttBridge will fail fast via RFCOMM if it is not an SPP device.
 */
final class SppDeviceHelper {
    private static final String TAG     = "LatrySppDeviceHelper";
    private static final UUID   SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private SppDeviceHelper() {}

    /**
     * Returns a JSON object {"name":..., "address":...} for the first
     * connected Classic BT device that supports SPP, or null if none found.
     */
    static String getFirstConnectedSppDeviceJson(Context context) {
        try {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                if (context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                        != PackageManager.PERMISSION_GRANTED) {
                    Log.w(TAG, "BLUETOOTH_CONNECT not granted");
                    return null;
                }
            }

            final BluetoothManager bluetoothManager =
                    (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
            if (bluetoothManager == null) return null;

            final BluetoothAdapter adapter = bluetoothManager.getAdapter();
            if (adapter == null || !adapter.isEnabled()) return null;

            final Set<BluetoothDevice> bonded = adapter.getBondedDevices();
            if (bonded == null || bonded.isEmpty()) return null;

            // Get BluetoothDevice.isConnected() via reflection
            Method isConnectedMethod = null;
            try {
                isConnectedMethod = BluetoothDevice.class.getMethod("isConnected");
            } catch (NoSuchMethodException e) {
                Log.w(TAG, "isConnected() not available, will try all Classic BT devices");
            }

            for (BluetoothDevice device : bonded) {
                // Skip BLE-only devices
                if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) continue;

                // Check ACL connection state
                if (isConnectedMethod != null) {
                    try {
                        boolean connected = (boolean) isConnectedMethod.invoke(device);
                        if (!connected) {
                            Log.d(TAG, "Skipping disconnected: " + device.getName());
                            continue;
                        }
                    } catch (Exception e) {
                        Log.w(TAG, "isConnected() failed for " + device.getName());
                    }
                }

                // Check for SPP UUID if available
                ParcelUuid[] uuids = device.getUuids();
                if (uuids != null) {
                    boolean hasSpp = false;
                    for (ParcelUuid uuid : uuids) {
                        if (uuid.getUuid().equals(SPP_UUID)) {
                            hasSpp = true;
                            break;
                        }
                    }
                    if (!hasSpp) {
                        Log.d(TAG, "Skipping non-SPP device: " + device.getName());
                        continue;
                    }
                } else {
                    // UUID cache empty – include and let SppPttBridge fail fast if not SPP
                    Log.d(TAG, "UUID cache empty for " + device.getName() + ", including anyway");
                }

                // First match – return it
                final String name = device.getName() != null ? device.getName() : "Unknown";
                Log.i(TAG, "Found SPP device: " + name + " (" + device.getAddress() + ")");
                final JSONObject result = new JSONObject();
                result.put("name", name);
                result.put("address", device.getAddress());
                return result.toString();
            }

            Log.i(TAG, "No connected SPP device found");
            return null;

        } catch (Exception e) {
            Log.e(TAG, "Error: " + e.getMessage());
            return null;
        }
    }
}