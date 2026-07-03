package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.io.IOException;
import java.io.InputStream;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class SppPttScanner {
    interface Callback {
        void onSppPttDeviceDetected(String name, String address,
                                    String pressPattern, String releasePattern);
    }

    private static final String TAG      = "LatrySppPttScanner";
    private static final UUID   SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int LEARN_TIMEOUT_MS = 12_000;
    private static final int SPLIT_WAIT_MS    = 150;

    private static volatile boolean scanning = false;
    private static ExecutorService executor;
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    private SppPttScanner() {}

    static boolean startScanning(Context context, Callback callback) {
        if (scanning) {
            toast(context, "SPP: Already scanning");
            return false;
        }

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            if (context.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                toast(context, "SPP: No BLUETOOTH_CONNECT permission!");
                return false;
            }
        }

        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            toast(context, "SPP: Bluetooth unavailable!");
            return false;
        }

        final Set<BluetoothDevice> paired = adapter.getBondedDevices();
        if (paired == null || paired.isEmpty()) {
            toast(context, "SPP: No paired devices!");
            return false;
        }

        int classicCount = 0;
        for (BluetoothDevice d : paired) {
            if (d.getType() != BluetoothDevice.DEVICE_TYPE_LE) classicCount++;
        }

        toast(context, "SPP: Scanning " + classicCount + " Classic BT device(s)...");
        scanning = true;
        executor = Executors.newCachedThreadPool();

        for (final BluetoothDevice device : paired) {
            if (device.getType() == BluetoothDevice.DEVICE_TYPE_LE) {
                Log.d(TAG, "Skipping LE device: " + device.getName());
                continue;
            }

            final String name    = device.getName() != null ? device.getName() : "Unknown";
            final String address = device.getAddress();

            executor.submit(() -> probeDevice(context, device, name, address, callback));
        }

        return true;
    }

    static void stopScanning() {
        if (!scanning) return;
        scanning = false;
        if (executor != null) {
            executor.shutdownNow();
            executor = null;
        }
        Log.i(TAG, "SPP scanning stopped");
    }

    static boolean isScanning() { return scanning; }

    private static void probeDevice(Context context,
                                    BluetoothDevice device,
                                    String name,
                                    String address,
                                    Callback callback) {
        BluetoothSocket socket = null;
        try {
            toast(context, "SPP: Connecting to " + name + "...");
            socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
            socket.connect();

            if (!scanning) return;
            toast(context, "SPP: Connected to " + name + " – press PTT!");

            learnPatterns(context, socket, name, address, callback);

        } catch (IOException e) {
            toast(context, "SPP: Cannot connect to " + name + ": " + e.getMessage());
        } finally {
            if (socket != null) {
                try { socket.close(); } catch (IOException ignored) {}
            }
        }
    }

    private static void learnPatterns(Context context,
                                      BluetoothSocket socket,
                                      String name,
                                      String address,
                                      Callback callback) {
        try {
            final InputStream stream   = socket.getInputStream();
            final byte[]      buf      = new byte[256];
            final long        deadline = System.currentTimeMillis() + LEARN_TIMEOUT_MS;

            String pressPattern   = null;
            String releasePattern = null;

            while (scanning && System.currentTimeMillis() < deadline) {

                if (stream.available() <= 0) {
                    Thread.sleep(20);
                    continue;
                }

                int n = stream.read(buf, 0, Math.min(stream.available(), buf.length));
                if (n <= 0) continue;

                Thread.sleep(SPLIT_WAIT_MS);
                if (stream.available() > 0) {
                    int n2 = stream.read(buf, n, Math.min(stream.available(), buf.length - n));
                    if (n2 > 0) n += n2;
                }

                final String chunk = new String(buf, 0, n);
                toast(context, "SPP: Got [" + chunk + "]");

                if (pressPattern == null) {
                    String[] parts = splitPressRelease(chunk);
                    pressPattern = parts[0];
                    toast(context, "SPP: Press=[" + pressPattern + "]");

                    if (parts[1] != null && !parts[1].isEmpty()
                            && !parts[1].equals(pressPattern)) {
                        releasePattern = parts[1];
                        toast(context, "SPP: Release=[" + releasePattern + "] DONE!");
                        onDetected(context, name, address,
                                   pressPattern, releasePattern, callback);
                        return;
                    }
                    toast(context, "SPP: Now release PTT...");

                } else {
                    String[] parts = splitPressRelease(chunk);
                    String candidate = parts[0];

                    if (candidate.equals(pressPattern)) {
                        toast(context, "SPP: Got press again, waiting for release...");
                        continue;
                    }

                    releasePattern = candidate;
                    toast(context, "SPP: Release=[" + releasePattern + "] DONE!");
                    onDetected(context, name, address,
                               pressPattern, releasePattern, callback);
                    return;
                }
            }

            if (pressPattern != null && scanning) {
                toast(context, "SPP: Timeout, saving with empty release");
                onDetected(context, name, address, pressPattern, "", callback);
            } else if (scanning) {
                toast(context, "SPP: Timeout, no data received from " + name);
            }

        } catch (IOException | InterruptedException e) {
            if (scanning) toast(context, "SPP: Error: " + e.getMessage());
        }
    }

    private static String[] splitPressRelease(String data) {
        if (data == null || data.isEmpty()) return new String[]{"", null};

        final String trimmed = data.trim();

        for (int splitAt = 1; splitAt <= trimmed.length() / 2; splitAt++) {
            final String first = trimmed.substring(0, splitAt);
            final String rest  = trimmed.substring(splitAt);

            if (!rest.startsWith(first)) {
                int nextPress = rest.indexOf(first);
                String release = nextPress > 0
                        ? rest.substring(0, nextPress).trim()
                        : rest.trim();

                if (!release.isEmpty() && !release.equals(first)) {
                    return new String[]{first, release};
                }
            }
        }

        return new String[]{trimmed, null};
    }

    private static void onDetected(Context context,
                                   String name, String address,
                                   String press, String release,
                                   Callback callback) {
        if (!scanning) return;
        scanning = false;

        HardwarePttSettingsStore.setLearnedSppDevice(
                context, name, address, press, release);

        mainHandler.post(() ->
                callback.onSppPttDeviceDetected(name, address, press, release));
    }

    private static void toast(Context context, String msg) {
        Log.i(TAG, msg);
        mainHandler.post(() ->
                Toast.makeText(context, msg, Toast.LENGTH_SHORT).show());
    }
}