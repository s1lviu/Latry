package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.content.pm.PackageManager;
import androidx.core.content.ContextCompat;

import java.io.IOException;
import java.io.InputStream;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Scans paired Classic Bluetooth (SPP/RFCOMM) devices during PTT learning
 * mode to detect any SPP PTT button.
 *
 * When a device sends ANY data while in learning mode, we record:
 *   1. The device name and address.
 *   2. The exact string sent on press (first message).
 *   3. The exact string sent on release (second message).
 *
 * This makes the bridge protocol-agnostic – it works with any SPP PTT
 * device regardless of whether it uses "+PTT=P", "PTT_ON", 0x01, or
 * any other convention.
 */
final class SppPttScanner {
    interface Callback {
        /**
         * Called on the main thread when a SPP PTT device press+release
         * sequence has been detected.
         *
         * @param name          Bluetooth device name
         * @param address       Bluetooth device MAC address
         * @param pressPattern  Raw string received on button press
         * @param releasePattern Raw string received on button release
         */
        void onSppPttDeviceDetected(String name, String address,
                                    String pressPattern, String releasePattern);
    }

    private static final String TAG = "LatrySppPttScanner";
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int READ_TIMEOUT_MS = 12_000;

    private static volatile boolean scanning = false;
    private static ExecutorService executor;
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    private SppPttScanner() {
    }

    static boolean startScanning(Context context, Callback callback) {
        if (scanning) {
            Log.w(TAG, "startScanning: already active");
            return false;
        }

        // Android 12+: BLUETOOTH_CONNECT is required to call getBondedDevices()
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(context,
                    android.Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                Log.w(TAG, "startScanning: BLUETOOTH_CONNECT permission not granted");
                return false;
            }
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

        for (final BluetoothDevice device : pairedDevices) {
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
            socket.connect();

            if (!scanning) {
                return;
            }

            Log.i(TAG, "SPP connected to: " + name + " – waiting for press+release");

            final InputStream stream = socket.getInputStream();
            final byte[] buf = new byte[64];
            final long deadline = System.currentTimeMillis() + READ_TIMEOUT_MS;

            String pressPattern = null;
            String releasePattern = null;

            while (scanning && System.currentTimeMillis() < deadline) {
                final int available = stream.available();
                if (available <= 0) {
                    Thread.sleep(20);
                    continue;
                }

                final int n = stream.read(buf, 0, Math.min(available, buf.length));
                if (n <= 0) {
                    continue;
                }

                // Trim whitespace/newlines and use raw string as pattern
                final String received = new String(buf, 0, n).trim();
                if (received.isEmpty()) {
                    continue;
                }

                Log.i(TAG, "SPP received from " + name + ": [" + received + "]");

                if (pressPattern == null) {
                    // First message = press pattern
                    pressPattern = received;
                    Log.i(TAG, "Learned press pattern: [" + pressPattern + "]");
                } else if (releasePattern == null && !received.equals(pressPattern)) {
                    // Second distinct message = release pattern
                    releasePattern = received;
                    Log.i(TAG, "Learned release pattern: [" + releasePattern + "]");

                    // We have both – report success
                    final String finalPress = pressPattern;
                    final String finalRelease = releasePattern;
                    onDeviceDetected(context, name, address,
                                     finalPress, finalRelease, callback);
                    return;
                }
            }

            if (pressPattern != null) {
                // Got press but no release within timeout – use empty string for release
                Log.w(TAG, "Got press pattern but no release from: " + name);
                onDeviceDetected(context, name, address, pressPattern, "", callback);
            } else {
                Log.d(TAG, "No PTT pattern received from: " + name + " within timeout");
            }

        } catch (IOException e) {
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
                                         String pressPattern,
                                         String releasePattern,
                                         Callback callback) {
        if (!scanning) {
            return;
        }
        scanning = false;

        HardwarePttSettingsStore.setLearnedSppDevice(
                context, name, address, pressPattern, releasePattern);
        Log.i(TAG, "Learned SPP PTT device: " + name
                + " press=[" + pressPattern + "]"
                + " release=[" + releasePattern + "]");

        mainHandler.post(() -> callback.onSppPttDeviceDetected(
                name, address, pressPattern, releasePattern));
    }
}