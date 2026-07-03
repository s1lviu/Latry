package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.content.pm.PackageManager;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.List;
import java.util.Set;

/**
 * Helper to retrieve currently connected Classic Bluetooth devices for
 * display in the SPP PTT device selection UI.
 *
 * Only returns devices that are actively connected (not just paired),
 * filtering out BLE-only devices. This avoids showing the user a long
 * list of every device they have ever paired with.
 */
final class SppDeviceHelper {
    private static final String TAG = "LatrySppDeviceHelper";

    private SppDeviceHelper() {}

    /**
     * Returns a JSON array of currently connected Classic BT devices, e.g.:
     * [{"name":"B02PTT-063F","address":"08:21:87:24:06:3F"}, ...]
     *
     * Returns empty array on error or no connected devices.
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
            if (bluetoothManager == null) {
                Log.w(TAG, "BluetoothManager unavailable");
                return "[]";
            }

            final BluetoothAdapter adapter = bluetoothManager.getAdapter();
            if (adapter == null || !adapter.isEnabled()) {
                Log.w(TAG, "Bluetooth adapter unavailable or disabled");
                return "[]";
            }

            final Set<BluetoothDevice> bonded = adapter.getBondedDevices();
            if (bonded == null || bonded.isEmpty()) {
                return "[]";
            }

            final JSONArray result = new JSONArray();

            for (BluetoothDevice device : bonded) {
                // Skip BLE-only devices – SPP requires Classic BT
                if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) {
                    continue;
                }

                // Check if device is currently connected via any Classic BT profile
                boolean isConnected =
                        bluetoothManager.getConnectionState(device, BluetoothProfile.HEADSET)
                                == BluetoothProfile.STATE_CONNECTED
                        || bluetoothManager.getConnectionState(device, BluetoothProfile.A2DP)
                                == BluetoothProfile.STATE_CONNECTED
                        || bluetoothManager.getConnectionState(device, BluetoothProfile.HEALTH)
                                == BluetoothProfile.STATE_CONNECTED;

                if (!isConnected) {
                    Log.d(TAG, "Skipping not-connected device: " + device.getName());
                    continue;
                }

                final JSONObject entry = new JSONObject();
                entry.put("name", device.getName() != null ? device.getName() : "Unknown");
                entry.put("address", device.getAddress());
                result.put(entry);
                Log.i(TAG, "Found connected Classic BT device: " + device.getName());
            }

            Log.i(TAG, "Returning " + result.length() + " connected Classic BT device(s)");
            return result.toString();

        } catch (Exception e) {
            Log.e(TAG, "Error getting connected devices: " + e.getMessage());
            return "[]";
        }
    }
}