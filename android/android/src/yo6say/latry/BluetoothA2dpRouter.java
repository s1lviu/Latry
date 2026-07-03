package yo6say.latry;

import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.content.pm.PackageManager;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.lang.reflect.Method;
import java.util.List;

/**
 * Manages Bluetooth A2DP device switching for audio routing.
 *
 * Uses BluetoothA2dp.getConnectedDevices() (public API) to enumerate all
 * connected A2DP devices, and reflection to call setActiveDevice() (hidden
 * API) to switch the active A2DP device when the user selects a specific
 * Bluetooth audio route.
 *
 * This allows Latry to use B02 as audio device even when another A2DP
 * device (e.g. car stereo) is currently active.
 */
final class BluetoothA2dpRouter {
    private static final String TAG = "LatryA2dpRouter";

    private static BluetoothA2dp a2dpProxy;
    private static final Object lock = new Object();

    private BluetoothA2dpRouter() {}

    /**
     * Initialize the A2DP proxy. Call once at app startup from LatryActivity.
     */
    static void init(Context context) {
        BluetoothAdapter adapter = getAdapter(context);
        if (adapter == null) return;

        adapter.getProfileProxy(context, new BluetoothProfile.ServiceListener() {
            @Override
            public void onServiceConnected(int profile, BluetoothProfile proxy) {
                if (profile == BluetoothProfile.A2DP) {
                    synchronized (lock) {
                        a2dpProxy = (BluetoothA2dp) proxy;
                    }
                    Log.i(TAG, "A2DP proxy connected");
                }
            }

            @Override
            public void onServiceDisconnected(int profile) {
                if (profile == BluetoothProfile.A2DP) {
                    synchronized (lock) {
                        a2dpProxy = null;
                    }
                    Log.w(TAG, "A2DP proxy disconnected");
                }
            }
        }, BluetoothProfile.A2DP);
    }

    /**
     * Returns JSON array of ALL connected A2DP devices, regardless of which
     * is currently active:
     * [{"name":"BMW Audio","address":"AA:BB:CC:DD:EE:FF"}, ...]
     */
    static String getConnectedA2dpDevicesJson(Context context) {
        try {
            if (!hasBluetoothPermission(context)) return "[]";

            synchronized (lock) {
                if (a2dpProxy == null) {
                    Log.w(TAG, "A2DP proxy not ready");
                    return "[]";
                }

                List<BluetoothDevice> devices = a2dpProxy.getConnectedDevices();
                if (devices == null || devices.isEmpty()) return "[]";

                JSONArray result = new JSONArray();
                for (BluetoothDevice device : devices) {
                    JSONObject entry = new JSONObject();
                    entry.put("name", device.getName() != null ? device.getName() : "Unknown");
                    entry.put("address", device.getAddress());
                    result.put(entry);
                    Log.i(TAG, "Connected A2DP: " + device.getName()
                            + " (" + device.getAddress() + ")");
                }
                return result.toString();
            }
        } catch (Exception e) {
            Log.e(TAG, "getConnectedA2dpDevicesJson: " + e.getMessage());
            return "[]";
        }
    }

    /**
     * Sets the active A2DP device by matching its product name against
     * the route ID (e.g. "B02PTT-063F" from "bluetooth:B02PTT-063F").
     *
     * Uses reflection to call BluetoothA2dp.setActiveDevice() (hidden API).
     * Returns true if the device was found and activated.
     */
    static boolean setActiveDeviceByProductName(Context context, String productName) {
        try {
            if (!hasBluetoothPermission(context)) return false;

            BluetoothDevice targetDevice = null;
            synchronized (lock) {
                if (a2dpProxy == null) {
                    Log.w(TAG, "A2DP proxy not ready");
                    return false;
                }

                List<BluetoothDevice> devices = a2dpProxy.getConnectedDevices();
                if (devices != null) {
                    for (BluetoothDevice device : devices) {
                        String name = device.getName();
                        if (name != null && name.equals(productName)) {
                            targetDevice = device;
                            break;
                        }
                    }
                }
            }

            if (targetDevice == null) {
                Log.w(TAG, "No A2DP device found with name: " + productName);
                return false;
            }

            // Call hidden setActiveDevice() via reflection
            synchronized (lock) {
                Method setActiveDevice = BluetoothA2dp.class.getMethod(
                        "setActiveDevice", BluetoothDevice.class);
                boolean result = (boolean) setActiveDevice.invoke(a2dpProxy, targetDevice);
                Log.i(TAG, "setActiveDevice(" + productName + ") -> " + result);
                return result;
            }

        } catch (Exception e) {
            Log.e(TAG, "setActiveDeviceByProductName failed: " + e.getMessage());
            return false;
        }
    }

    private static BluetoothAdapter getAdapter(Context context) {
        BluetoothManager manager =
                (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        return manager != null ? manager.getAdapter() : null;
    }

    private static boolean hasBluetoothPermission(Context context) {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            return context.checkSelfPermission(
                    android.Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }
}