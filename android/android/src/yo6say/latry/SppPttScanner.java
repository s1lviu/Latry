package yo6say.latry;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Listens for incoming Bluetooth SPP/RFCOMM connections during PTT learning
 * mode. Instead of trying to connect OUT to devices (which fails when the
 * device already has an active connection), we open a server socket and let
 * the PTT device connect IN to us — exactly how normal SppPttBridge operation
 * works.
 *
 * When the PTT button is pressed, the device connects and sends data.
 * We record the first message as the press pattern, wait for a second
 * distinct message as the release pattern, then report success.
 */
final class SppPttScanner {
    interface Callback {
        void onSppPttDeviceDetected(String name, String address,
                                    String pressPattern, String releasePattern);
    }

    private static final String TAG = "LatrySppPttScanner";
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final String SERVICE_NAME = "LatryPttLearning";
    private static final int READ_TIMEOUT_MS = 12_000;

    private static volatile boolean scanning = false;
    private static BluetoothServerSocket serverSocket;
    private static ExecutorService executor;
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    private SppPttScanner() {
    }

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

        try {
            // Open a server socket – the PTT device will connect to us
            serverSocket = adapter.listenUsingRfcommWithServiceRecord(
                    SERVICE_NAME, SPP_UUID);
        } catch (IOException e) {
            Log.e(TAG, "Failed to open Bluetooth server socket: " + e.getMessage());
            return false;
        }

        scanning = true;
        executor = Executors.newSingleThreadExecutor();
        executor.submit(() -> acceptConnection(context, callback));

        Log.i(TAG, "SPP learning server started, waiting for PTT device to connect...");
        return true;
    }

    static void stopScanning() {
        if (!scanning) {
            return;
        }
        scanning = false;

        // Close server socket to unblock accept()
        if (serverSocket != null) {
            try { serverSocket.close(); } catch (IOException ignored) { }
            serverSocket = null;
        }

        if (executor != null) {
            executor.shutdownNow();
            executor = null;
        }
        Log.i(TAG, "SPP learning server stopped");
    }

    static boolean isScanning() {
        return scanning;
    }

    private static void acceptConnection(Context context, Callback callback) {
        BluetoothSocket socket = null;
        try {
            Log.i(TAG, "Waiting for incoming PTT device connection...");

            // accept() blocks until a device connects or the socket is closed
            socket = serverSocket.accept();

            if (!scanning) {
                return;
            }

            final BluetoothDevice device = socket.getRemoteDevice();
            final String name = device.getName() != null ? device.getName() : "Unknown";
            final String address = device.getAddress();
            Log.i(TAG, "PTT device connected: " + name + " (" + address + ")");

            // Now read press and release patterns
            learnPatterns(context, socket, name, address, callback);

        } catch (IOException e) {
            if (scanning) {
                Log.e(TAG, "Server socket error: " + e.getMessage());
            }
        } finally {
            if (socket != null) {
                try { socket.close(); } catch (IOException ignored) { }
            }
            if (serverSocket != null) {
                try { serverSocket.close(); } catch (IOException ignored) { }
                serverSocket = null;
            }
        }
    }

    private static void learnPatterns(Context context,
                                      BluetoothSocket socket,
                                      String name,
                                      String address,
                                      Callback callback) {
        try {
            final InputStream stream = socket.getInputStream();
            final byte[] buf = new byte[64];
            final long deadline = System.currentTimeMillis() + READ_TIMEOUT_MS;
            final StringBuilder rxBuffer = new StringBuilder();

            String pressPattern = null;
            String releasePattern = null;

            while (scanning && System.currentTimeMillis() < deadline) {
                final int available = stream.available();
                if (available <= 0) {
                    Thread.sleep(20);
                    continue;
                }

                final int n = stream.read(buf, 0, Math.min(available, buf.length));
                if (n <= 0) continue;

                rxBuffer.append(new String(buf, 0, n));
                Log.d(TAG, "Raw buffer: [" + rxBuffer + "]");

                // Process complete messages - try newline first, then fixed chunks
                while (true) {
                    String candidate = null;
                    int consumeUpTo = -1;

                    int nlIdx = rxBuffer.indexOf("\n");
                    if (nlIdx >= 0) {
                        candidate = rxBuffer.substring(0, nlIdx).trim();
                        consumeUpTo = nlIdx + 1;
                    } else if (rxBuffer.length() >= 6) {
                        // No newline - B02 sends exactly 6 chars without delimiter
                        candidate = rxBuffer.substring(0, 6).trim();
                        consumeUpTo = 6;
                    } else {
                        break; // Need more data
                    }

                    if (consumeUpTo > 0) {
                        rxBuffer.delete(0, consumeUpTo);
                    }

                    if (candidate == null || candidate.isEmpty()) {
                        continue;
                    }

                    Log.i(TAG, "PTT candidate: [" + candidate + "]");

                    if (pressPattern == null) {
                        pressPattern = candidate;
                        Log.i(TAG, "Learned press pattern: [" + pressPattern + "]");
                    } else if (!candidate.equals(pressPattern)) {
                        releasePattern = candidate;
                        Log.i(TAG, "Learned release pattern: [" + releasePattern + "]");

                        // Success!
                        final String fp = pressPattern;
                        final String fr = releasePattern;
                        onDeviceDetected(context, name, address, fp, fr, callback);
                        return;
                    }
                }
            }

            // Timeout - if we got press but no release, save with empty release
            if (pressPattern != null) {
                Log.w(TAG, "Got press but no release, saving with empty release pattern");
                onDeviceDetected(context, name, address, pressPattern, "", callback);
            } else {
                Log.w(TAG, "No patterns learned within timeout");
            }

        } catch (IOException | InterruptedException e) {
            if (scanning) {
                Log.e(TAG, "Error reading from PTT device: " + e.getMessage());
            }
        }
    }

    private static void onDeviceDetected(Context context,
                                         String name,
                                         String address,
                                         String pressPattern,
                                         String releasePattern,
                                         Callback callback) {
        if (!scanning) return;
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