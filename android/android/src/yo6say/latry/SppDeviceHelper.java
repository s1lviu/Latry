package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Method;
import java.util.Set;

/**
 * Helper to retrieve currently connected Classic Bluetooth devices for
 * display in the SPP PTT device selection UI.
 *
 * Uses reflection to call BluetoothDevice.isConnected() (hidden API) which
 * checks the ACL connection state — the lowest-level connection check that
 * works for all Classic BT profiles including SPP devices like the Inrico B02.
 */
final class SppDeviceHelper {
    private static final String TAG = "LatrySppDeviceHelper";

    private SppDeviceHelper() {}

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
            if (adapter == null || !adapter.isEnabled()) return "[]";

            final Set<BluetoothDevice> bonded = adapter.getBondedDevices();
            if (bonded == null || bonded.isEmpty()) return "[]";

            // Get BluetoothDevice.isConnected() via reflection (hidden API)
            // This checks ACL connection state and works for all profiles
            // including SPP devices that don't use standard profiles.
            Method isConnectedMethod = null;
            try {
                isConnectedMethod = BluetoothDevice.class.getMethod("isConnected");
            } catch (NoSuchMethodException e) {
                Log.w(TAG, "isConnected() not available via reflection, showing all paired devices");
            }

            final JSONArray result = new JSONArray();

            for (BluetoothDevice device : bonded) {
                // Skip BLE-only devices
                if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) continue;

                // Check connection state via reflection if available
                if (isConnectedMethod != null) {
                    try {
                        boolean connected = (boolean) isConnectedMethod.invoke(device);
                        if (!connected) {
                            Log.d(TAG, "Skipping disconnected device: " + device.getName());
                            continue;
                        }
                    } catch (Exception e) {
                        Log.w(TAG, "isConnected() failed for " + device.getName() + ", including anyway");
                    }
                }

                final JSONObject entry = new JSONObject();
                entry.put("name", device.getName() != null ? device.getName() : "Unknown");
                entry.put("address", device.getAddress());
                result.put(entry);
                Log.i(TAG, "Connected Classic BT device: " + device.getName());
            }

            Log.i(TAG, "Found " + result.length() + " connected Classic BT device(s)");
            return result.toString();

        } catch (Exception e) {
            Log.e(TAG, "Error: " + e.getMessage());
            return "[]";
        }
    }
}