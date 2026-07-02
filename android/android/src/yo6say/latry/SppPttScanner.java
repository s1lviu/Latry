package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Scans paired Classic Bluetooth (SPP/RFCOMM) devices during PTT learning
 * mode to detect Zello-protocol PTT buttons such as the Inrico B02.
 *
 * When learning mode starts, this scanner attempts an RFCOMM connection to
 * each paired Classic BT device in turn. If a device sends "+PTT=P" (or
 * any case variant) within the learning timeout, the device's name and
 * address are reported via the {@link Callback} interface and saved to
 * {@link HardwarePttSettingsStore}.
 *
 * Lifecycle: call {@link #startScanning} when learning mode begins, and
 * {@link #stopScanning} when it ends (timeout, cancel, or key capture).
 * Only one scan may be active at a time.
 */
final class SppPttScanner {
    interface Callback {
        /** Called on the main thread when a SPP PTT device is detected. */
        void onSppPttDeviceDetected(String name, String address);
    }

    private static final String TAG = "LatrySppPttScanner";
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int READ_TIMEOUT_MS = 12_000;
    private static final int CONNECT_TIMEOUT_MS = 3_000;
    private static final byte[] PTT_PRESS_PATTERN = "+ptt=p".getBytes();

    private static volatile boolean scanning = false;
    private static ExecutorService executor;
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    private SppPttScanner() {
    }

    /**
     * Starts scanning paired SPP devices. Each device is probed on a
     * background thread. Stops automatically on first detection.
     *
     * @param context  Android context (used for permission checks and storage).
     * @param callback Notified on main thread when a SPP PTT device is found.
     * @return true if scanning started, false if already scanning or BT unavailable.
     */
    static boolean startScanning(Context context, Callback callback) {
        if (scanning) {
            Log.w(TAG, "startScanning: already active");
            return false;
        }

        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            Log.w(TAG, "startScanning: Bluetooth adapter unavailable");
            return false;
        }

        final Set<BluetoothDevice> pairedDevices = adapter.getBondedDevices();
        if (pairedDevices == null || pairedDevices.isEmpty()) {
            Log.i(TAG, "startScanning: no paired devices");
            return false;
        }

        scanning = true;
        executor = Executors.newCachedThreadPool();

        // Probe each paired Classic BT device in parallel on background threads.
        for (final BluetoothDevice device : pairedDevices) {
            // Skip BLE-only devices (they have no SPP RFCOMM channel).
            if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) {
                Log.d(TAG, "Skipping LE-only device: " + device.getName());
                continue;
            }

            final String name = device.getName() != null ? device.getName() : "Unknown";
            final String address = device.getAddress();
            Log.i(TAG, "Probing SPP device: " + name + " (" + address + ")");

            executor.submit(() -> probeDevice(context, device, name, address, callback));
        }

        Log.i(TAG, "SPP scanning started for " + pairedDevices.size() + " paired device(s)");
        return true;
    }

    /**
     * Stops all active SPP probes immediately.
     * Safe to call even if not scanning.
     */
    static void stopScanning() {
        if (!scanning) {
            return;
        }
        scanning = false;
        if (executor != null) {
            executor.shutdownNow();
            executor = null;
        }
        Log.i(TAG, "SPP scanning stopped");
    }

    static boolean isScanning() {
        return scanning;
    }

    private static void probeDevice(Context context,
                                    BluetoothDevice device,
                                    String name,
                                    String address,
                                    Callback callback) {
        BluetoothSocket socket = null;
        try {
            socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
            socket.connect(); // blocks up to system timeout (~10s for RFCOMM)

            if (!scanning) {
                return; // another device was already detected
            }

            Log.i(TAG, "SPP connected to: " + name + " – listening for PTT pattern");

            final InputStream stream = socket.getInputStream();
            final byte[] buf = new byte[64];
            final long deadline = System.currentTimeMillis() + READ_TIMEOUT_MS;
            final StringBuilder received = new StringBuilder();

            while (scanning && System.currentTimeMillis() < deadline) {
                final int available = stream.available();
                if (available <= 0) {
                    Thread.sleep(20);
                    continue;
                }

                final int n = stream.read(buf, 0, Math.min(available, buf.length));
                if (n > 0) {
                    received.append(new String(buf, 0, n).toLowerCase());
                    Log.d(TAG, "SPP received from " + name + ": " + received);

                    if (received.toString().contains("+ptt=p")) {
                        Log.i(TAG, "SPP PTT press detected from: " + name
                                + " (" + address + ")");
                        onDeviceDetected(context, name, address, callback);
                        return;
                    }

                    // Limit buffer size – we only need a few characters
                    if (received.length() > 128) {
                        received.delete(0, received.length() - 32);
                    }
                }
            }

            Log.d(TAG, "No PTT pattern received from: " + name + " within timeout");

        } catch (IOException e) {
            // Normal – most paired devices won't have a SPP server running
            Log.d(TAG, "SPP connect/read failed for " + name + ": " + e.getMessage());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } finally {
            if (socket != null) {
                try { socket.close(); } catch (IOException ignored) { }
            }
        }
    }

    private static void onDeviceDetected(Context context,
                                         String name,
                                         String address,
                                         Callback callback) {
        // Only the first detection wins
        if (!scanning) {
            return;
        }
        scanning = false;

        HardwarePttSettingsStore.setLearnedSppDevice(context, name, address);
        Log.i(TAG, "Learned SPP PTT device: " + name + " (" + address + ")");

        mainHandler.post(() -> callback.onSppPttDeviceDetected(name, address));
    }
}