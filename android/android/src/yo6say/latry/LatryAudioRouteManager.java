package yo6say.latry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import androidx.core.content.ContextCompat;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothManager;
import java.lang.reflect.Method;

public final class LatryAudioRouteManager {
    private static final String TAG = "LatryAudioRouteMgr";
    private static final String ROUTE_SPEAKER = LatryAudioRoutePolicy.ROUTE_SPEAKER;
    private static final String ROUTE_WIRED_HEADSET = LatryAudioRoutePolicy.ROUTE_WIRED_HEADSET;
    private static final String ROUTE_BLUETOOTH = LatryAudioRoutePolicy.ROUTE_BLUETOOTH;
    private static final int TYPE_BLE_HEADSET = 26;
    private static final int TYPE_BLE_SPEAKER = 27;
    private static final long BLUETOOTH_CAPTURE_ROUTE_TIMEOUT_MS = 1500L;
    private static final long BLUETOOTH_CAPTURE_ROUTE_POLL_MS = 75L;

    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    private static Context appContext;
    private static AudioManager audioManager;
    private static AudioDeviceCallback audioDeviceCallback;
    private static BroadcastReceiver headsetPlugReceiver;
    private static AudioManager.OnCommunicationDeviceChangedListener communicationDeviceChangedListener;
    private static boolean monitoring = false;
    private static String preferredRoute = ROUTE_SPEAKER;
    private static String currentRoute = ROUTE_SPEAKER;
    private static String availableRoutesJson = LatryAudioRoutePolicy.routesJson(Collections.singletonList(ROUTE_SPEAKER));

    private LatryAudioRouteManager() {
    }

    public static synchronized void startMonitoring(Context context) {
        ensureInitialized(context);
        if (audioManager == null) {
            dispatchAudioRoutesChanged(currentRoute, availableRoutesJson);
            return;
        }

        if (!monitoring) {
            registerCallbacksLocked();
            monitoring = true;
        }

        refreshStateLocked(true);
    }

    public static synchronized void stopMonitoring() {
        if (audioManager == null || !monitoring) {
            return;
        }

        unregisterCallbacksLocked();
        monitoring = false;
    }

    public static synchronized void setPreferredRoute(Context context, String routeId) {
        ensureInitialized(context);
        preferredRoute = normalizeRouteId(routeId);
        refreshStateLocked(true);
    }

    public static synchronized void reapplyPreferredRoute(Context context) {
        ensureInitialized(context);
        refreshStateLocked(true);
    }

    public static synchronized boolean prepareCaptureRoute(Context context, String routeId) {
        ensureInitialized(context);
        String normalizedRoute = normalizeRouteId(routeId);
        if (audioManager == null) {
            return !ROUTE_BLUETOOTH.equals(normalizedRoute);
        }

        if (!ROUTE_BLUETOOTH.equals(normalizedRoute)) {
            applyRouteLocked(normalizedRoute);
            return true;
        }

        return activateBluetoothCaptureRouteLocked();
    }

    public static synchronized void finishCaptureRoute(Context context, String routeId) {
        ensureInitialized(context);
        if (audioManager == null || !ROUTE_BLUETOOTH.equals(normalizeRouteId(routeId))) {
            return;
        }

        Log.i(TAG, "Releasing Bluetooth communication route after TX");
        releaseBluetoothCommunicationRouteLocked();
        refreshStateLocked(true);
    }

    private static void releaseBluetoothCommunicationRouteLocked() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                audioManager.clearCommunicationDevice();
            } catch (Exception e) {
                Log.w(TAG, "Failed to clear Bluetooth communication route after TX", e);
            }
        } else {
            stopBluetoothScoLocked();
            setSpeakerphoneOnLocked(false);
        }

        try {
            audioManager.setMode(AudioManager.MODE_NORMAL);
        } catch (Exception e) {
            Log.w(TAG, "Failed to restore MODE_NORMAL after Bluetooth TX", e);
        }
    }

    public static synchronized String getCurrentRoute() {
        return currentRoute;
    }

    public static synchronized String getAvailableRoutesJson() {
        return availableRoutesJson;
    }

    public static synchronized String getPreferredRoute() {
        return preferredRoute;
    }

    private static void ensureInitialized(Context context) {
        if (audioManager != null) {
            return;
        }

        if (context == null) {
            Log.w(TAG, "Cannot initialize audio route manager without a Context");
            return;
        }

        appContext = context.getApplicationContext();
        Object systemService = appContext.getSystemService(Context.AUDIO_SERVICE);
        if (!(systemService instanceof AudioManager)) {
            Log.w(TAG, "AudioManager service is not available");
            return;
        }

        audioManager = (AudioManager) systemService;
        audioDeviceCallback = new AudioDeviceCallback() {
            @Override
            public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
                Log.i(TAG, "AudioDeviceCallback: devices added (" + addedDevices.length + ")");
                postRefresh();
            }

            @Override
            public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
                Log.i(TAG, "AudioDeviceCallback: devices removed (" + removedDevices.length + ")");
                postRefresh();
            }
        };

        headsetPlugReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                if (AudioManager.ACTION_HEADSET_PLUG.equals(intent.getAction())) {
                    int state = intent.getIntExtra("state", -1);
                    Log.i(TAG, "ACTION_HEADSET_PLUG: state=" + state
                            + " mic=" + intent.getIntExtra("microphone", -1));
                    postRefresh();
                }
            }
        };

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            communicationDeviceChangedListener = new AudioManager.OnCommunicationDeviceChangedListener() {
                @Override
                public void onCommunicationDeviceChanged(AudioDeviceInfo device) {
                    postRefresh();
                }
            };
        }
    }

    private static void registerCallbacksLocked() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && audioDeviceCallback != null) {
            try {
                audioManager.registerAudioDeviceCallback(audioDeviceCallback, mainHandler);
            } catch (Exception e) {
                Log.w(TAG, "Failed to register AudioDeviceCallback", e);
            }
        }

        if (headsetPlugReceiver != null && appContext != null) {
            try {
                IntentFilter filter = new IntentFilter(AudioManager.ACTION_HEADSET_PLUG);
                ContextCompat.registerReceiver(
                        appContext,
                        headsetPlugReceiver,
                        filter,
                        null,
                        mainHandler,
                        ContextCompat.RECEIVER_NOT_EXPORTED);
            } catch (Exception e) {
                Log.w(TAG, "Failed to register headset plug receiver", e);
            }
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && communicationDeviceChangedListener != null) {
            try {
                audioManager.addOnCommunicationDeviceChangedListener(
                    appContext.getMainExecutor(),
                    communicationDeviceChangedListener);
            } catch (Exception e) {
                Log.w(TAG, "Failed to register communication device listener", e);
            }
        }
    }

    private static void unregisterCallbacksLocked() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && communicationDeviceChangedListener != null) {
            try {
                audioManager.removeOnCommunicationDeviceChangedListener(communicationDeviceChangedListener);
            } catch (Exception e) {
                Log.w(TAG, "Failed to unregister communication device listener", e);
            }
        }

        if (headsetPlugReceiver != null && appContext != null) {
            try {
                appContext.unregisterReceiver(headsetPlugReceiver);
            } catch (Exception e) {
                Log.w(TAG, "Failed to unregister headset plug receiver", e);
            }
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && audioDeviceCallback != null) {
            try {
                audioManager.unregisterAudioDeviceCallback(audioDeviceCallback);
            } catch (Exception e) {
                Log.w(TAG, "Failed to unregister AudioDeviceCallback", e);
            }
        }
    }

    private static void postRefresh() {
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                synchronized (LatryAudioRouteManager.class) {
                    refreshStateLocked(false);
                }
            }
        });
    }

    private static void refreshStateLocked(boolean forceNotify) {
        if (audioManager == null) {
            return;
        }

        List<String> availableRoutes = detectAvailableRoutesLocked();
        String targetRoute = chooseTargetRouteLocked(availableRoutes);
        String systemCurrentRoute = detectSystemCurrentRouteLocked(availableRoutes);

        if (!targetRoute.equals(systemCurrentRoute)) {
            applyRouteLocked(targetRoute);
            availableRoutes = detectAvailableRoutesLocked();
            systemCurrentRoute = detectSystemCurrentRouteLocked(availableRoutes);
        }

        final String nextCurrentRoute = resolveCurrentRouteLocked(systemCurrentRoute, availableRoutes);
        final String nextAvailableRoutesJson = toRoutesJsonLocked(availableRoutes);
        final boolean changed = !nextCurrentRoute.equals(currentRoute)
                || !nextAvailableRoutesJson.equals(availableRoutesJson);

        currentRoute = nextCurrentRoute;
        availableRoutesJson = nextAvailableRoutesJson;

        if (forceNotify || changed) {
            dispatchAudioRoutesChanged(currentRoute, availableRoutesJson);
        }
    }

    static synchronized boolean isMonitoringForTesting() {
        return monitoring;
    }

    private static String chooseTargetRouteLocked(List<String> availableRoutes) {
        return LatryAudioRoutePolicy.chooseTargetRoute(availableRoutes, preferredRoute);
    }

    private static List<String> detectAvailableRoutesLocked() {
        List<String> routes = new ArrayList<>();
        if (audioManager == null) {
            routes.add(ROUTE_SPEAKER);
            return routes;
        }

        List<AudioDeviceInfo> devices = getRouteDevicesLocked();
        for (AudioDeviceInfo deviceInfo : devices) {
            String routeId = routeIdForDevice(deviceInfo);
            if (routeId.isEmpty()) {
                if (!isExpectedNonRouteDeviceType(deviceInfo.getType())) {
                    Log.d(TAG, "Skipping unrecognized device type=" + deviceInfo.getType()
                            + " product=" + deviceInfo.getProductName());
                }
                continue;
            }
            if (routes.contains(routeId)) {
                continue;
            }
            Log.d(TAG, "Detected route: " + routeId + " (type=" + deviceInfo.getType() + ")");
            routes.add(routeId);
        }

        // Include all connected Classic BT devices that support A2DP,
        // even if they are not the currently active audio output.
        // Uses reflection to check ACL connection state (same safe approach
        // as SppDeviceHelper) — purely passive, no system state manipulation.
        if (appContext != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                BluetoothManager btManager = (BluetoothManager)
                        appContext.getSystemService(Context.BLUETOOTH_SERVICE);
                if (btManager != null) {
                    android.bluetooth.BluetoothAdapter adapter = btManager.getAdapter();
                    if (adapter != null && adapter.isEnabled()) {
                        java.lang.reflect.Method isConnected =
                                android.bluetooth.BluetoothDevice.class.getMethod("isConnected");
                        for (android.bluetooth.BluetoothDevice device
                                : adapter.getBondedDevices()) {
                            if (device.getType()
                                    == android.bluetooth.BluetoothDevice.DEVICE_TYPE_LE) {
                                continue;
                            }
                            try {
                                if (!(boolean) isConnected.invoke(device)) continue;
                            } catch (Exception ignored) {}

                            String routeId = LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX
                                    + device.getName();
                            if (!routes.contains(routeId)) {
                                Log.d(TAG, "Adding connected BT device: " + routeId);
                                routes.add(routeId);
                            }
                        }
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to enumerate connected BT devices: " + e.getMessage());
            }
        }

        return LatryAudioRoutePolicy.orderedAvailableRoutes(routes);
    }

    private static String detectSystemCurrentRouteLocked(List<String> availableRoutes) {
        if (audioManager == null) {
            return "";
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            AudioDeviceInfo communicationDevice = audioManager.getCommunicationDevice();
            if (communicationDevice != null) {
                String routeId = routeIdForDevice(communicationDevice);
                if (!routeId.isEmpty()) {
                    return routeId;
                }
            }
        }

        if (isBluetoothScoOnLocked()) {
            return ROUTE_BLUETOOTH;
        }

        // Pre-API 31 with MODE_NORMAL: Android's default AudioPolicy routes
        // USAGE_MEDIA to wired headphones when connected (device priority:
        // wired > speaker).  isSpeakerphoneOn() is unreliable in MODE_NORMAL
        // — it may return a stale value that doesn't reflect actual routing.
        // Check wired device presence first; only fall back to speakerphone
        // query when no wired device is connected.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            if (availableRoutes.contains(ROUTE_WIRED_HEADSET)) {
                // Wired headset is physically connected — in MODE_NORMAL
                // Android routes USAGE_MEDIA here by default, unless user
                // explicitly asked for speaker.
                if (ROUTE_SPEAKER.equals(preferredRoute) && isSpeakerphoneOnLocked()) {
                    return ROUTE_SPEAKER;
                }
                return ROUTE_WIRED_HEADSET;
            }
            if (isSpeakerphoneOnLocked()) {
                return ROUTE_SPEAKER;
            }
        }

        return "";
    }

    private static String resolveCurrentRouteLocked(String systemCurrentRoute, List<String> availableRoutes) {
        if (!systemCurrentRoute.isEmpty()) {
            return normalizeRouteId(systemCurrentRoute);
        }

        return chooseTargetRouteLocked(availableRoutes);
    }

    private static List<AudioDeviceInfo> getRouteDevicesLocked() {
        List<AudioDeviceInfo> devices = new ArrayList<>();

        if (audioManager == null) {
            return devices;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            devices.addAll(audioManager.getAvailableCommunicationDevices());
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return devices;
        }

        AudioDeviceInfo[] outputDevices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        if (outputDevices == null) {
            return devices;
        }

        for (AudioDeviceInfo deviceInfo : outputDevices) {
            devices.add(deviceInfo);
        }
        return devices;
    }

    static synchronized AudioDeviceInfo findPlaybackDevice(Context context, String routeId) {
        ensureInitialized(context);
        return findPlaybackDeviceLocked(normalizeRouteId(routeId));
    }

    static synchronized AudioDeviceInfo findCaptureDevice(Context context, String routeId) {
        ensureInitialized(context);
        return findCaptureDeviceLocked(normalizeRouteId(routeId));
    }

    static synchronized AudioDeviceInfo findBuiltInMic(Context context) {
        ensureInitialized(context);
        return findBuiltInMicLocked();
    }

private static AudioDeviceInfo findPlaybackDeviceLocked(String routeId) {
        if (audioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return null;
        }

        if (LatryAudioRoutePolicy.isBluetoothRoute(routeId)) {
            if (routeId.startsWith(LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX)) {
                String targetName = routeId.substring(
                        LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX.length());
                AudioDeviceInfo[] outputDevices =
                        audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
                if (outputDevices != null) {
                    for (AudioDeviceInfo deviceInfo : outputDevices) {
                        if (isBluetoothType(deviceInfo.getType())) {
                            CharSequence name = deviceInfo.getProductName();
                            if (name != null && targetName.equals(name.toString())) {
                                return deviceInfo;
                            }
                        }
                    }
                }
            }
            return findBestBluetoothPlaybackDeviceLocked();
        }

        AudioDeviceInfo[] outputDevices =
                audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        if (outputDevices == null) return null;

        for (AudioDeviceInfo deviceInfo : outputDevices) {
            String deviceRouteId = routeIdForDevice(deviceInfo);
            if (!deviceRouteId.isEmpty() && deviceRouteId.equals(routeId)) {
                return deviceInfo;
            }
        }
        return null;
    }

    private static AudioDeviceInfo findCaptureDeviceLocked(String routeId) {
        if (audioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return null;
        }

        if (LatryAudioRoutePolicy.isBluetoothRoute(routeId)) {
            if (routeId.startsWith(LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX)) {
                String targetName = routeId.substring(
                        LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX.length());
                AudioDeviceInfo[] inputDevices =
                        audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
                if (inputDevices != null) {
                    for (AudioDeviceInfo deviceInfo : inputDevices) {
                        if (isBluetoothCaptureType(deviceInfo.getType())) {
                            CharSequence name = deviceInfo.getProductName();
                            if (name != null && targetName.equals(name.toString())) {
                                return deviceInfo;
                            }
                        }
                    }
                }
            }
            return findBestBluetoothCaptureDeviceLocked();
        }

        AudioDeviceInfo[] inputDevices =
                audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
        if (inputDevices == null) return null;

        for (AudioDeviceInfo deviceInfo : inputDevices) {
            String deviceRouteId = captureRouteIdForDevice(deviceInfo);
            if (!deviceRouteId.isEmpty() && deviceRouteId.equals(routeId)) {
                return deviceInfo;
            }
        }

        if (ROUTE_SPEAKER.equals(routeId)) {
            return findBuiltInMicLocked();
        }
        return null;
    }

    private static AudioDeviceInfo findBestBluetoothPlaybackDeviceLocked() {
        AudioDeviceInfo leAudio = null;
        AudioDeviceInfo a2dp = null;
        AudioDeviceInfo bleSpeaker = null;
        AudioDeviceInfo sco = null;
        AudioDeviceInfo[] outputDevices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        if (outputDevices == null) {
            return null;
        }

        for (AudioDeviceInfo deviceInfo : outputDevices) {
            int deviceType = deviceInfo.getType();
            if (deviceType == TYPE_BLE_HEADSET && leAudio == null) {
                leAudio = deviceInfo;
            } else if (deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP && a2dp == null) {
                a2dp = deviceInfo;
            } else if (deviceType == TYPE_BLE_SPEAKER && bleSpeaker == null) {
                bleSpeaker = deviceInfo;
            } else if (deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO && sco == null) {
                sco = deviceInfo;
            }
        }

        if (leAudio != null) {
            return leAudio;
        }
        if (a2dp != null) {
            return a2dp;
        }
        if (bleSpeaker != null) {
            return bleSpeaker;
        }
        return sco;
    }

    private static AudioDeviceInfo findBestBluetoothCaptureDeviceLocked() {
        AudioDeviceInfo leAudio = null;
        AudioDeviceInfo sco = null;
        AudioDeviceInfo[] inputDevices = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
        if (inputDevices == null) {
            return null;
        }

        for (AudioDeviceInfo deviceInfo : inputDevices) {
            int deviceType = deviceInfo.getType();
            if (deviceType == TYPE_BLE_HEADSET && leAudio == null) {
                leAudio = deviceInfo;
            } else if (deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO && sco == null) {
                sco = deviceInfo;
            }
        }

        return leAudio != null ? leAudio : sco;
    }

    private static AudioDeviceInfo findBestBluetoothCommunicationDeviceLocked() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return null;
        }

        AudioDeviceInfo leAudio = null;
        AudioDeviceInfo sco = null;
        List<AudioDeviceInfo> devices = audioManager.getAvailableCommunicationDevices();
        for (AudioDeviceInfo deviceInfo : devices) {
            int deviceType = deviceInfo.getType();
            if (deviceType == TYPE_BLE_HEADSET && leAudio == null) {
                leAudio = deviceInfo;
            } else if (deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO && sco == null) {
                sco = deviceInfo;
            }
        }

        return leAudio != null ? leAudio : sco;
    }

    private static AudioDeviceInfo findBuiltInMicLocked() {
        if (audioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return null;
        }

        AudioDeviceInfo[] inputDevices = audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS);
        if (inputDevices == null) {
            return null;
        }

        for (AudioDeviceInfo deviceInfo : inputDevices) {
            if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC) {
                return deviceInfo;
            }
        }

        return null;
    }

    @SuppressWarnings("deprecation")
    private static void applyRouteLocked(String routeId) {
        String normalizedRoute = normalizeRouteId(routeId);
        Log.i(TAG, "applyRouteLocked: " + normalizedRoute);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (LatryAudioRoutePolicy.isBluetoothRoute(normalizedRoute)) {
                try {
                    audioManager.clearCommunicationDevice();
                } catch (Exception e) {
                    Log.w(TAG, "Failed to clear communication device for Bluetooth media route", e);
                }

                return;
            }

            AudioDeviceInfo deviceInfo = findDeviceForRouteLocked(normalizedRoute);
            if (deviceInfo == null) {
                if (ROUTE_SPEAKER.equals(normalizedRoute)) {
                    clearRouteSelectionLocked();
                    setSpeakerphoneOnLocked(true);
                    return;
                }
                Log.w(TAG, "Requested route is not available: " + normalizedRoute);
                return;
            }

            try {
                boolean success = audioManager.setCommunicationDevice(deviceInfo);
                Log.i(TAG, "setCommunicationDevice(" + normalizedRoute + ") -> " + success);
            } catch (Exception e) {
                Log.w(TAG, "Failed to set communication device for route " + normalizedRoute, e);
            }
            return;
        }

        // Pre-API 31 with MODE_NORMAL: Android's default AudioPolicy routes
        // USAGE_MEDIA to wired headphones when connected.  We only need
        // setSpeakerphoneOn(true) to force speaker when wired is connected,
        // or setSpeakerphoneOn(false) to return to default device priority.
        clearRouteSelectionLocked();

        if (ROUTE_BLUETOOTH.equals(normalizedRoute)) {
            setSpeakerphoneOnLocked(false);
            Log.i(TAG, "Applied Bluetooth media route; SCO will be activated only for TX");
            return;
        }

        if (ROUTE_SPEAKER.equals(normalizedRoute)) {
            setSpeakerphoneOnLocked(true);
            return;
        }

        setSpeakerphoneOnLocked(false);
        Log.i(TAG, "Applied wired route, speakerphoneOn=" + isSpeakerphoneOnLocked());
    }

    private static void clearRouteSelectionLocked() {
        if (audioManager == null) {
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                audioManager.clearCommunicationDevice();
            } catch (Exception e) {
                Log.w(TAG, "Failed to clear communication device", e);
            }
        }

        stopBluetoothScoLocked();
        setSpeakerphoneOnLocked(false);
    }

    private static AudioDeviceInfo findDeviceForRouteLocked(String routeId) {
        String normalizedRoute = normalizeRouteId(routeId);
        for (AudioDeviceInfo deviceInfo : getRouteDevicesLocked()) {
            String deviceRouteId = routeIdForDevice(deviceInfo);
            if (!deviceRouteId.isEmpty() && deviceRouteId.equals(normalizedRoute)) {
                return deviceInfo;
            }
        }
        return null;
    }

    private static String routeIdForDevice(AudioDeviceInfo deviceInfo) {
        if (deviceInfo == null) {
            return "";
        }

        int deviceType = deviceInfo.getType();
        if (deviceType == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
            return ROUTE_SPEAKER;
        }
        if (isWiredHeadsetType(deviceType)) {
            return ROUTE_WIRED_HEADSET;
        }
        if (isBluetoothType(deviceType)) {
            CharSequence productName = deviceInfo.getProductName();
            if (productName != null && productName.length() > 0) {
                return LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX + productName.toString();
            }
            return ROUTE_BLUETOOTH;
        }
        return "";
    }

    private static String captureRouteIdForDevice(AudioDeviceInfo deviceInfo) {
        if (deviceInfo == null) {
            return "";
        }

        int deviceType = deviceInfo.getType();
        if (deviceType == AudioDeviceInfo.TYPE_BUILTIN_MIC) {
            return ROUTE_SPEAKER;
        }
        if (isWiredCaptureType(deviceType)) {
            return ROUTE_WIRED_HEADSET;
        }
        if (isBluetoothCaptureType(deviceType)) {
            CharSequence productName = deviceInfo.getProductName();
            if (productName != null && productName.length() > 0) {
                return LatryAudioRoutePolicy.ROUTE_BLUETOOTH_PREFIX + productName.toString();
            }
            return ROUTE_BLUETOOTH;
        }
        return "";
    }

    private static boolean isExpectedNonRouteDeviceType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
                || deviceType == AudioDeviceInfo.TYPE_TELEPHONY;
    }

    private static boolean isWiredHeadsetType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_WIRED_HEADSET
                || deviceType == AudioDeviceInfo.TYPE_WIRED_HEADPHONES
                || deviceType == AudioDeviceInfo.TYPE_USB_HEADSET
                || deviceType == AudioDeviceInfo.TYPE_USB_DEVICE;
    }

    private static boolean isBluetoothType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP
                || deviceType == TYPE_BLE_HEADSET
                || deviceType == TYPE_BLE_SPEAKER;
    }

    private static boolean isWiredCaptureType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_WIRED_HEADSET
                || deviceType == AudioDeviceInfo.TYPE_USB_HEADSET
                || deviceType == AudioDeviceInfo.TYPE_USB_DEVICE
                || deviceType == AudioDeviceInfo.TYPE_USB_ACCESSORY;
    }

    private static boolean isBluetoothCaptureType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || deviceType == TYPE_BLE_HEADSET;
    }

    private static String normalizeRouteId(String routeId) {
        return LatryAudioRoutePolicy.normalizeRouteId(routeId);
    }

    private static String toRoutesJsonLocked(List<String> routes) {
        return LatryAudioRoutePolicy.routesJson(routes);
    }

    private static boolean activateBluetoothCaptureRouteLocked() {
        if (audioManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return false;
        }

        try {
            audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        } catch (Exception e) {
            Log.w(TAG, "Failed to set MODE_IN_COMMUNICATION for Bluetooth TX", e);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            AudioDeviceInfo communicationDevice = findBestBluetoothCommunicationDeviceLocked();
            if (communicationDevice == null) {
                Log.w(TAG, "Bluetooth TX requested but no SCO/LE communication device is available");
                releaseBluetoothCommunicationRouteLocked();
                return false;
            }

            try {
                boolean success = audioManager.setCommunicationDevice(communicationDevice);
                Log.i(TAG, "setCommunicationDevice(bluetooth_tx, type="
                        + communicationDevice.getType() + ") -> " + success);
                if (!success) {
                    releaseBluetoothCommunicationRouteLocked();
                    return false;
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to activate Bluetooth communication device for TX", e);
                releaseBluetoothCommunicationRouteLocked();
                return false;
            }
        } else {
            startBluetoothScoLocked();
        }

        AudioDeviceInfo captureDevice = waitForBluetoothCaptureDeviceLocked();
        if (captureDevice == null) {
            Log.w(TAG, "Bluetooth TX requested but no SCO/LE capture device became available");
            releaseBluetoothCommunicationRouteLocked();
            return false;
        }

        Log.i(TAG, "Bluetooth capture route ready type=" + captureDevice.getType()
                + " product=" + captureDevice.getProductName());
        return true;
    }

    private static AudioDeviceInfo waitForBluetoothCaptureDeviceLocked() {
        long deadline = SystemClock.uptimeMillis() + BLUETOOTH_CAPTURE_ROUTE_TIMEOUT_MS;
        AudioDeviceInfo captureDevice = findBestBluetoothCaptureDeviceLocked();
        while (captureDevice == null && SystemClock.uptimeMillis() < deadline) {
            SystemClock.sleep(BLUETOOTH_CAPTURE_ROUTE_POLL_MS);
            captureDevice = findBestBluetoothCaptureDeviceLocked();
        }
        return captureDevice;
    }

    @SuppressWarnings("deprecation")
    private static boolean isSpeakerphoneOnLocked() {
        return audioManager != null && audioManager.isSpeakerphoneOn();
    }

    @SuppressWarnings("deprecation")
    private static void setSpeakerphoneOnLocked(boolean enabled) {
        if (audioManager == null) {
            return;
        }
        audioManager.setSpeakerphoneOn(enabled);
    }

    @SuppressWarnings("deprecation")
    private static boolean isBluetoothScoOnLocked() {
        return audioManager != null && audioManager.isBluetoothScoOn();
    }

    @SuppressWarnings("deprecation")
    private static void startBluetoothScoLocked() {
        if (audioManager == null || Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return;
        }
        audioManager.startBluetoothSco();
        audioManager.setBluetoothScoOn(true);
    }

    @SuppressWarnings("deprecation")
    private static void stopBluetoothScoLocked() {
        if (audioManager == null || Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return;
        }
        if (audioManager.isBluetoothScoOn()) {
            audioManager.stopBluetoothSco();
        }
        audioManager.setBluetoothScoOn(false);
    }

    private static void dispatchAudioRoutesChanged(String currentRoute, String availableRoutesJson) {
        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordAudioRoutesChanged(currentRoute, availableRoutesJson);
            return;
        }
        notifyAudioRoutesChanged(currentRoute, availableRoutesJson);
    }

    private static native void notifyAudioRoutesChanged(String currentRoute, String availableRoutesJson);
}
