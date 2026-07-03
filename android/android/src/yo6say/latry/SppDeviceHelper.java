package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.Context;
import android.content.pm.PackageManager;
import android.util.Log;
import android.bluetooth.BluetoothManager;
import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Set;

/**
 * Helper to retrieve paired Classic Bluetooth devices for display in the
 * SPP PTT device selection UI.
 */
final class SppDeviceHelper {
    private static final String TAG = "LatrySppDeviceHelper";

    private SppDeviceHelper() {}

    /**
     * Returns a JSON array of paired Classic BT devices, e.g.:
     * [{"name":"B02PTT-XXXX","address":"XX:XX:XX:XX:XX:XX"}, ...]
     *
     * Returns empty array on error or no devices.
     */
    static String getPairedClassicDevicesJson(Context context) {
        try {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                if (context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                        != PackageManager.PERMISSION_GRANTED) {
                    Log.w(TAG, "BLUETOOTH_CONNECT not granted");
                    return "[]";
                }
            }

            final BluetoothManager bluetoothManager = 
                (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
            if (bluetoothManager == null) return "[]";
            final BluetoothAdapter adapter = bluetoothManager.getAdapter();
            if (adapter == null || !adapter.isEnabled()) {
                return "[]";
            }

            final Set<BluetoothDevice> paired = adapter.getBondedDevices();
            if (paired == null || paired.isEmpty()) {
                return "[]";
            }

            final JSONArray result = new JSONArray();
            for (BluetoothDevice device : paired) {
                // Skip BLE-only devices
                if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) continue;

                final JSONObject entry = new JSONObject();
                entry.put("name", device.getName() != null ? device.getName() : "Unknown");
                entry.put("address", device.getAddress());
                result.put(entry);
            }

            return result.toString();

        } catch (Exception e) {
            Log.e(TAG, "Error getting paired devices: " + e.getMessage());
            return "[]";
        }
    }
}