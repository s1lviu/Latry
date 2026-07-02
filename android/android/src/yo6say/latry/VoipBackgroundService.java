package yo6say.latry;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.ForegroundServiceStartNotAllowedException;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.graphics.Color;
import android.media.AudioManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;
import android.app.ActivityManager;
import android.os.Handler;
import android.os.Looper;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;
import org.qtproject.qt.android.QtServiceBase;

public class VoipBackgroundService extends QtServiceBase {
    private static final String TAG = "VoipBackgroundService";
    static final String ACTION_START_VOIP = "START_VOIP";
    private static final String ACTION_CONTROL_EVENT = "CONTROL_EVENT";
    public static final String ACTION_PTT_PRESSED = "yo6say.latry.action.PTT_PRESSED";
    public static final String ACTION_PTT_RELEASED = "yo6say.latry.action.PTT_RELEASED";
    private static final String ACTION_STOP_VOIP = "STOP_VOIP";
    private static final String EXTRA_CONTROL_EVENT = "control_event";
    static final String EXTRA_MONITOR_CONNECTION = "monitor_connection";
    private static final int NOTIFICATION_ID = 1001;
    private static final String CHANNEL_ID = "voip_service_channel";
    private static final String CHANNEL_NAME = "VoIP Background Service";
    
    // Service state
    private static boolean isServiceRunning = false;
    private static VoipBackgroundService instance = null;
    
    // Connection state
    private volatile String serverHost = "";
    private volatile int serverPort = 0;
    private volatile int talkGroup = 0;
    private volatile String callsign = "";
    private volatile String connectionStatus = "Connecting...";
    private volatile String currentTalker = "";
    private volatile boolean isConnected = false;
    private volatile boolean isReceiving = false;
    private volatile boolean isTransmitting = false;
    private volatile boolean isMuted = false;
    private volatile boolean foregroundStarted = false;
    private volatile boolean connectionMonitoringEnabled = false;
    private volatile int connectionStatusUpdateGeneration = 0;
    private volatile int receiveEventGeneration = 0;
    private volatile String lastReceiveEventTalker = "";
    private volatile int lastDefaultNetworkGeneration = 0;
    private volatile int lastDefaultNetworkTransport = NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
    private LatryMediaSessionManager mediaSessionManager;
    private NetworkHandoverMonitor networkHandoverMonitor;
    private boolean networkMonitorActive = false;
    private PTTButtonBroadcastReceiver backgroundMeigPttReceiver;
    
    // System locks
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private AudioManager audioManager;
    
    // Keep-alive mechanism
    private Handler keepAliveHandler;
    private Runnable keepAliveRunnable;
    private static boolean nativeBridgeAvailable = true;
    private boolean qtServiceAttached = false;
    
    @Override
    public void onCreate() {
        Log.d(TAG, "VoIP Background Service bootstrap create");
        LatrySentry.addBreadcrumb("service.lifecycle", "service_created", io.sentry.SentryLevel.INFO);
        instance = this;
        createNotificationChannel();
        if (!foregroundStarted) {
            startForegroundWithType(NOTIFICATION_ID, createNotification());
            foregroundStarted = true;
            Log.d(TAG, "Foreground bootstrap notification started before Qt initialization");
        }

        if (isQtRuntimeStarted() || isQtLoaderConflict()) {
            Log.d(TAG, "Qt runtime already active in app process - using controller-only service mode");
        } else {
            try {
                super.onCreate();
                qtServiceAttached = true;
                Log.d(TAG, "Qt service lifecycle attached for headless service mode");
            } catch (ClassCastException e) {
                Log.w(TAG, "QtLoader singleton conflict (Activity loader occupied m_instance during "
                        + "deferred initialization window) - falling back to controller-only mode", e);
            }
        }

        Log.d(TAG, "VoIP Background Service created");
        
        // Initialize system managers
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            // Use Huawei-whitelisted wake lock tag to prevent force release during freeze cycles
            String wakeLockTag = "Latry:VoipService";
            if ("Huawei".equals(android.os.Build.MANUFACTURER)) {
                wakeLockTag = "LocationManagerService"; // Whitelisted by Huawei's power management
                Log.d(TAG, "Using Huawei-compatible wake lock tag");
            }
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, wakeLockTag);
            wakeLock.setReferenceCounted(false);
        }
        
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm != null) {
            wifiLock = createWifiLockCompat(wm, "Latry:VoipService");
            wifiLock.setReferenceCounted(false);
        }
        
        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        
        mediaSessionManager = new LatryMediaSessionManager(
            getApplicationContext(),
            new LatryMediaSessionManager.ControlEventListener() {
                @Override
                public void onControlEvent(int eventType) {
                    handleAndroidControlEvent(eventType);
                }
            });
        mediaSessionManager.initialize();

        networkHandoverMonitor = new NetworkHandoverMonitor(
                getApplicationContext(),
                new NetworkHandoverMonitor.Listener() {
                    @Override
                    public void onNetworkSnapshotChanged(NetworkHandoverMonitor.NetworkSnapshot snapshot) {
                        handleNetworkSnapshot(snapshot);
                    }
                });

        registerBackgroundMeigPttReceiver();
    }
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "Qt VoIP Service onStartCommand");
        Map<String, Object> startData = new HashMap<>();
        startData.put("has_intent", intent != null);
        startData.put("start_id", startId);
        startData.put("flags", flags);
        startData.put("action", intent != null && intent.getAction() != null ? intent.getAction() : "null");
        LatrySentry.addBreadcrumb("service.lifecycle", "service_start_command",
                io.sentry.SentryLevel.INFO, startData);

        if (intent == null) {
            Log.d(TAG, "Null start intent received — stopping service (auto-restart is disabled)");
            stopSelf();
            return START_NOT_STICKY;
        }

        String action = intent.getAction();

        if (ACTION_START_VOIP.equals(action)) {
            serverHost = valueOrEmpty(intent.getStringExtra("host"));
            serverPort = intent.getIntExtra("port", 0);
            talkGroup = intent.getIntExtra("talkgroup", 0);
            callsign = valueOrEmpty(intent.getStringExtra("callsign"));
            connectionMonitoringEnabled = intent.getBooleanExtra(EXTRA_MONITOR_CONNECTION, false);
            connectionStatus = connectionMonitoringEnabled
                    ? buildConnectingStatus(serverHost, serverPort)
                    : buildDisconnectedStatus();

            startVoipService();
            return START_REDELIVER_INTENT;
        }

        if (ACTION_CONTROL_EVENT.equals(action)) {
            ensureForegroundControllerForExternalControl();
            handleAndroidControlEvent(intent.getIntExtra(EXTRA_CONTROL_EVENT, 0));
            return START_STICKY;
        }

        if (ACTION_PTT_PRESSED.equals(action) || ACTION_PTT_RELEASED.equals(action)) {
            ensureForegroundControllerForExternalControl();
            handleAndroidControlEvent(ACTION_PTT_PRESSED.equals(action)
                    ? LatryMediaSessionManager.EVENT_PTT_PRESS
                    : LatryMediaSessionManager.EVENT_PTT_RELEASE);
            return START_STICKY;
        }

        if (ACTION_STOP_VOIP.equals(action)) {
            stopVoipServiceInternal();
            return START_NOT_STICKY;
        }

        Log.w(TAG, "Ignoring unknown service action: " + action);
        return START_NOT_STICKY;
    }

    private String valueOrEmpty(String value) {
        return value != null ? value : "";
    }

    private void applySavedReconnectProfileIfNeeded() {
        if (!serverHost.isEmpty() && serverPort > 0) {
            return;
        }

        if (!ConnectionProfileStore.hasSavedConnectionProfile(getApplicationContext())) {
            return;
        }

        serverHost = valueOrEmpty(ConnectionProfileStore.getSavedHost(getApplicationContext()));
        serverPort = ConnectionProfileStore.getSavedPort(getApplicationContext());
        callsign = valueOrEmpty(ConnectionProfileStore.getSavedCallsign(getApplicationContext()));
        talkGroup = ConnectionProfileStore.getSavedTalkgroup(getApplicationContext());
    }

    private String buildConnectingStatus(String host, int port) {
        return VoipServiceStatusFormatter.buildConnectingStatus(host, port);
    }

    private String buildDisconnectedStatus() {
        return VoipServiceStatusFormatter.buildDisconnectedStatus(serverHost, serverPort);
    }

    private void configureConnectionMonitoring(boolean enabled) {
        connectionMonitoringEnabled = enabled;
        if (enabled) {
            startNetworkMonitor();
            startKeepAlive();
        } else {
            stopNetworkMonitor();
            stopKeepAlive();
        }
    }

    private void startNetworkMonitor() {
        if (networkHandoverMonitor == null) {
            networkMonitorActive = false;
            return;
        }
        networkMonitorActive = networkHandoverMonitor.start();
        Log.d(TAG, "Default network monitor active=" + networkMonitorActive);
    }

    private void stopNetworkMonitor() {
        if (networkHandoverMonitor != null) {
            networkHandoverMonitor.stop();
        }
        networkMonitorActive = false;
    }

    private void ensureForegroundControllerForExternalControl() {
        if (isServiceRunning) {
            return;
        }

        applySavedReconnectProfileIfNeeded();
        connectionStatus = buildDisconnectedStatus();
        startVoipService();
    }
    
    private void startVoipService() {
        Log.d(TAG, "Starting Qt VoIP foreground service");
        LatrySentry.addBreadcrumb("service.lifecycle", "service_foreground_started",
                io.sentry.SentryLevel.INFO);
        isServiceRunning = true;
        isConnected = false;
        isReceiving = false;
        isTransmitting = false;
        currentTalker = "";

        if (mediaSessionManager != null) {
            mediaSessionManager.updateConnectionStatus(false, callsign, talkGroup);
            mediaSessionManager.updateCurrentTalker("");
            mediaSessionManager.updateRXStatus(false, "");
            mediaSessionManager.updateTXStatus(false);
        }
        
        // Check battery optimization status
        checkBatteryOptimization();
        
        // Acquire system locks
        acquireSystemLocks();

        Notification notification = createNotification();
        if (!foregroundStarted) {
            startForegroundWithType(NOTIFICATION_ID, notification);
            foregroundStarted = true;
        } else {
            publishNotification(notification);
        }

        configureConnectionMonitoring(connectionMonitoringEnabled);
        syncForegroundState();
        
        // Notify Qt application that service is ready
        dispatchServiceStartedCallback();
    }
    
    private void stopVoipServiceInternal() {
        Log.d(TAG, "Stopping Qt VoIP service");
        LatrySentry.addBreadcrumb("service.lifecycle", "service_stop_requested",
                io.sentry.SentryLevel.INFO);
        isServiceRunning = false;
        isConnected = false;
        isReceiving = false;
        isTransmitting = false;
        currentTalker = "";

        if (mediaSessionManager != null) {
            mediaSessionManager.updateCurrentTalker("");
            mediaSessionManager.updateRXStatus(false, "");
            mediaSessionManager.updateTXStatus(false);
            mediaSessionManager.updateConnectionStatus(false, callsign, talkGroup);
        }
        
        // Stop keep-alive mechanism
        configureConnectionMonitoring(false);
        
        // Release system locks
        releaseSystemLocks();
        
        // Notify Qt application that service is stopping
        dispatchServiceStoppedCallback();
        
        // Stop foreground service
        if (foregroundStarted) {
            stopForegroundCompat();
            foregroundStarted = false;
        }
        stopSelf();
    }
    
    private void checkBatteryOptimization() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null && !pm.isIgnoringBatteryOptimizations(getPackageName())) {
                Log.w(TAG, "WARNING: App is subject to battery optimization. VoIP may be killed in background!");
                Log.i(TAG, "Please disable battery optimization for " + getPackageName() + " in Settings");
            } else {
                Log.d(TAG, "Battery optimization is disabled - VoIP should work reliably in background");
            }
        }
    }
    
    private void startKeepAlive() {
        if (!connectionMonitoringEnabled) {
            Log.d(TAG, "Skipping keep-alive start because connection monitoring is disabled");
            return;
        }

        if (keepAliveHandler == null) {
            keepAliveHandler = new Handler(Looper.getMainLooper());
        }

        stopKeepAlive();
        
        keepAliveRunnable = new Runnable() {
            @Override
            public void run() {
                if (isServiceRunning) {
                    if (!connectionMonitoringEnabled) {
                        Log.d(TAG, "Keep-alive tick skipped because connection monitoring is disabled");
                        return;
                    }

                    // Renew wake lock if needed
                    if (wakeLock != null && !wakeLock.isHeld()) {
                        wakeLock.acquire(10*60*1000L); // 10 minutes timeout
                        Log.d(TAG, "Wake lock renewed during freeze/unfreeze cycle");
                    }
                    
                    // Trigger Qt reconnection check for TCP disconnection (with delay for Qt readiness)
                    keepAliveHandler.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            dispatchCheckConnectionCallback();
                        }
                    }, 100); // Small delay to ensure Qt is ready
                    
                    // Reconcile foreground state on each keep-alive tick.
                    syncForegroundState();
                    
                    // Schedule next keep-alive in 15 seconds for very aggressive monitoring during freeze cycles
                    keepAliveHandler.postDelayed(this, 15*1000L);
                }
            }
        };
        
        // Start first keep-alive in 15 seconds for faster detection
        keepAliveHandler.postDelayed(keepAliveRunnable, 15*1000L);
        Log.d(TAG, "Very aggressive keep-alive mechanism started (15 second intervals)");
    }
    
    private void stopKeepAlive() {
        if (keepAliveHandler != null && keepAliveRunnable != null) {
            keepAliveHandler.removeCallbacks(keepAliveRunnable);
            Log.d(TAG, "Keep-alive mechanism stopped");
        }
    }
    
    private void acquireSystemLocks() {
        if (wakeLock != null && !wakeLock.isHeld()) {
            // Acquire with timeout to prevent permanent locks
            wakeLock.acquire(10*60*1000L); // 10 minutes timeout, will be renewed
            Log.d(TAG, "Wake lock acquired with timeout (Huawei-compatible tag)");
        }
        
        if (wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
            Log.d(TAG, "WiFi lock acquired");
        }
        
        // Set process importance to prevent hibernation
        setProcessImportance();
    }
    
    private void setProcessImportance() {
        try {
            ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null) {
                // This is a hint to the system that this process should not be killed
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_AUDIO);
                Log.d(TAG, "Process priority set to URGENT_AUDIO");
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not set process importance", e);
        }
    }
    
    private void releaseSystemLocks() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
            Log.d(TAG, "Wake lock released");
        }
        
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
            Log.d(TAG, "WiFi lock released");
        }
    }

    private void startForegroundWithType(int notificationId, Notification notification) {
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(notificationId, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        } else {
            startForeground(notificationId, notification);
        }
    }

    private void upgradeForegroundForMicrophone() {
        if (Build.VERSION.SDK_INT >= 34 && foregroundStarted) {
            startForeground(NOTIFICATION_ID, createNotification(),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
                            | ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            Log.d(TAG, "Foreground service upgraded to include MICROPHONE type for PTT");
        }
    }

    private void stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } else {
            stopForegroundLegacy();
        }
    }

    @SuppressWarnings("deprecation")
    private void stopForegroundLegacy() {
        stopForeground(true);
    }

    private WifiManager.WifiLock createWifiLockCompat(WifiManager wifiManager, String tag) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, tag);
        }
        return createLegacyWifiLock(wifiManager, tag);
    }

    @SuppressWarnings("deprecation")
    private WifiManager.WifiLock createLegacyWifiLock(WifiManager wifiManager, String tag) {
        return wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, tag);
    }
    
    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("VoIP background service notifications - keeps connection alive");
            channel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
            channel.setShowBadge(true);
            channel.enableVibration(false);
            channel.setSound(null, null);
            channel.setBypassDnd(false);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                channel.setAllowBubbles(false);
            }
            
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }
    
    private Notification createNotification() {
        // Create intent to open main activity
        Intent openAppIntent = new Intent(this, LatryActivity.class);
        openAppIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent openAppPendingIntent = PendingIntent.getActivity(
            this, 0, openAppIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        
        // No disconnect action - keep it simple
        
        // Determine status color and icon
        int statusColor = isConnected ? Color.GREEN : Color.YELLOW;
        int notificationIcon = android.R.drawable.ic_media_play;
        
        // Build Qt-compliant notification for VoIP
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(buildNotificationTitle())
            .setContentText(buildNotificationText())
            .setSmallIcon(notificationIcon)
            .setColor(statusColor)
            .setContentIntent(openAppPendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)
            .setShowWhen(true)
            .setWhen(System.currentTimeMillis())
            .setAutoCancel(false)
            .setCategory(NotificationCompat.CATEGORY_CALL)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .setOnlyAlertOnce(true);
        
        // No action buttons - keep notification simple
        
        // Set expanded style
        NotificationCompat.BigTextStyle bigTextStyle = new NotificationCompat.BigTextStyle()
            .setBigContentTitle("Latry App Service")
            .bigText(buildDetailedNotificationText());
        builder.setStyle(bigTextStyle);
        
        return builder.build();
    }
    
    private String buildNotificationTitle() {
        return VoipServiceStatusFormatter.buildNotificationTitle(
            isConnected,
            isReceiving,
            isTransmitting);
    }

    private String buildNotificationText() {
        return VoipServiceStatusFormatter.buildNotificationText(
            isConnected,
            isReceiving,
            isTransmitting,
            talkGroup,
            currentTalker,
            connectionStatus,
            serverHost,
            serverPort);
    }
    
    private String buildDetailedNotificationText() {
        return VoipServiceStatusFormatter.buildDetailedNotificationText(
            connectionStatus,
            callsign,
            talkGroup,
            isReceiving,
            isTransmitting,
            currentTalker);
    }
    
    public static void startVoipService(Context context, String host, int port, String callsign, int talkgroup) {
        startVoipService(context, host, port, callsign, talkgroup, true);
    }

    public static void startVoipService(Context context, String host, int port, String callsign,
                                        int talkgroup, boolean monitorConnection) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        intent.setAction(ACTION_START_VOIP);
        intent.putExtra("host", host);
        intent.putExtra("port", port);
        intent.putExtra("callsign", callsign);
        intent.putExtra("talkgroup", talkgroup);
        intent.putExtra(EXTRA_MONITOR_CONNECTION, monitorConnection);
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
        Map<String, Object> requestData = new HashMap<>();
        requestData.put("monitor_connection", monitorConnection);
        requestData.put("sdk_int", Build.VERSION.SDK_INT);
        LatrySentry.addBreadcrumb("service.lifecycle", "service_start_requested",
                io.sentry.SentryLevel.INFO, requestData);
        Log.d(TAG, "VoIP service start requested with parameters, monitorConnection=" + monitorConnection);
    }

    public static void ensureControllerService(Context context) {
        if (instance != null && isServiceRunning) {
            return;
        }

        if (!ConnectionProfileStore.hasSavedConnectionProfile(context)) {
            Log.d(TAG, "Skipping controller service auto-start because no saved reconnect profile exists");
            return;
        }

        String host = "";
        int port = 0;
        String callsign = "";
        int talkgroup = 0;

        host = ConnectionProfileStore.getSavedHost(context);
        port = ConnectionProfileStore.getSavedPort(context);
        callsign = ConnectionProfileStore.getSavedCallsign(context);
        talkgroup = ConnectionProfileStore.getSavedTalkgroup(context);

        startVoipService(context, host, port, callsign, talkgroup, false);
    }

    public static void dispatchControlEvent(Context context, int eventType) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        intent.setAction(ACTION_CONTROL_EVENT);
        intent.putExtra(EXTRA_CONTROL_EVENT, eventType);

        startServiceSafely(context, intent);
        Log.d(TAG, "Control event dispatched to service: " + eventType);
    }

    public static void dispatchPTTAction(Context context, boolean pressed) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        intent.setAction(pressed ? ACTION_PTT_PRESSED : ACTION_PTT_RELEASED);

        startServiceSafely(context, intent);
        Log.d(TAG, "PTT action dispatched to service: " + (pressed ? "pressed" : "released"));
    }
    
    private static void startServiceSafely(Context context, Intent intent) {
        if (isServiceRunning) {
            // Service is already running as foreground — just deliver the intent.
            context.startService(intent);
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                context.startForegroundService(intent);
            } catch (ForegroundServiceStartNotAllowedException e) {
                Log.w(TAG, "Cannot start foreground service from background: " + e.getMessage());
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void stopVoipService(Context context) {
        final VoipBackgroundService serviceToStop = instance;
        if (serviceToStop == null) {
            boolean stopRequested = context.stopService(new Intent(context, VoipBackgroundService.class));
            Log.d(TAG, "VoIP service stop requested without active instance, stopService()=" + stopRequested);
            return;
        }

        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(() -> {
            if (serviceToStop == instance) {
                serviceToStop.stopVoipServiceInternal();
            } else {
                Log.d(TAG, "Ignoring stale VoIP service stop request for replaced instance");
            }
        });
        Log.d(TAG, "VoIP service stop requested");
    }
    
    public static boolean isRunning() {
        return isServiceRunning;
    }

    public static boolean shouldClaimOrderedPttBroadcasts() {
        final VoipBackgroundService activeInstance = instance;
        return activeInstance != null && activeInstance.isConnected;
    }

    boolean isForegroundStartedForTesting() {
        return foregroundStarted;
    }

    boolean isConnectedForTesting() {
        return isConnected;
    }

    boolean isReceivingForTesting() {
        return isReceiving;
    }

    boolean isTransmittingForTesting() {
        return isTransmitting;
    }

    String getCurrentTalkerForTesting() {
        return currentTalker;
    }

    int getConnectionStatusUpdateGenerationForTesting() {
        return connectionStatusUpdateGeneration;
    }

    int getReceiveEventGenerationForTesting() {
        return receiveEventGeneration;
    }

    String getLastReceiveEventTalkerForTesting() {
        return lastReceiveEventTalker;
    }

    int getLastDefaultNetworkGenerationForTesting() {
        return lastDefaultNetworkGeneration;
    }

    int getLastDefaultNetworkTransportForTesting() {
        return lastDefaultNetworkTransport;
    }

    boolean isQtServiceAttachedForTesting() {
        return qtServiceAttached;
    }

    boolean isConnectionMonitoringEnabledForTesting() {
        return connectionMonitoringEnabled;
    }

    boolean isNetworkMonitorActiveForTesting() {
        return networkMonitorActive;
    }

    String getConnectionStatusForTesting() {
        return connectionStatus;
    }

    String getServerHostForTesting() {
        return serverHost;
    }

    int getServerPortForTesting() {
        return serverPort;
    }

    int getTalkGroupForTesting() {
        return talkGroup;
    }

    boolean hasMediaSessionForTesting() {
        return mediaSessionManager != null;
    }

    public void setConnectionMonitoringEnabled(boolean enabled) {
        configureConnectionMonitoring(enabled);
        if (!enabled && !isConnected) {
            connectionStatus = buildDisconnectedStatus();
            syncForegroundState();
        }
        Log.d(TAG, "Qt service - Connection monitoring enabled: " + enabled);
    }
    
    // Qt-compliant update methods
    public void updateConnectionStatus(String status, boolean connected) {
        String previousStatus = this.connectionStatus;
        boolean previousConnected = this.isConnected;
        boolean previousReceiving = this.isReceiving;
        boolean previousTransmitting = this.isTransmitting;
        String previousTalker = this.currentTalker;

        this.connectionStatus = status;
        this.isConnected = connected;
        if (!connected) {
            this.isReceiving = false;
            this.isTransmitting = false;
            this.currentTalker = "";
            if (!connectionMonitoringEnabled && (status == null || status.isEmpty() || "Disconnected".equals(status))) {
                this.connectionStatus = buildDisconnectedStatus();
            }
        }
        if (mediaSessionManager != null) {
            mediaSessionManager.updateConnectionStatus(connected, callsign, talkGroup);
        }
        syncForegroundState();
        if (!Objects.equals(previousStatus, this.connectionStatus)
                || previousConnected != this.isConnected
                || previousReceiving != this.isReceiving
                || previousTransmitting != this.isTransmitting
                || !Objects.equals(previousTalker, this.currentTalker)) {
            connectionStatusUpdateGeneration++;
            Log.d(TAG, "Qt service - Connection status updated: " + this.connectionStatus);
        }
    }
    
    public void updateCurrentTalker(String talker) {
        String normalizedTalker = talker != null ? talker : "";
        String previousTalker = this.currentTalker;
        this.currentTalker = normalizedTalker;
        if (mediaSessionManager != null) {
            mediaSessionManager.updateCurrentTalker(normalizedTalker);
        }
        syncForegroundState();
        if (!Objects.equals(previousTalker, this.currentTalker)) {
            Log.d(TAG, "Qt service - Current talker updated: " + this.currentTalker);
        }
    }

    public void updateTalkgroup(int talkgroup) {
        int previousTalkgroup = this.talkGroup;
        this.talkGroup = talkgroup;
        if (mediaSessionManager != null) {
            mediaSessionManager.updateTalkgroup(talkgroup);
        }
        syncForegroundState();
        if (previousTalkgroup != this.talkGroup) {
            Log.d(TAG, "Qt service - Talkgroup updated: " + this.talkGroup);
        }
    }

    public void updateReceiveState(boolean receiving, String talker) {
        boolean previousReceiving = this.isReceiving;
        String previousTalker = this.currentTalker;
        this.isReceiving = receiving;
        if (!receiving && (talker == null || talker.isEmpty())) {
            this.currentTalker = "";
        } else if (talker != null && !talker.isEmpty()) {
            this.currentTalker = talker;
        }
        if (mediaSessionManager != null) {
            mediaSessionManager.updateRXStatus(receiving, this.currentTalker);
        }
        syncForegroundState();
        boolean changed = previousReceiving != this.isReceiving
                || !Objects.equals(previousTalker, this.currentTalker);
        if (changed && this.isReceiving && !this.currentTalker.isEmpty()) {
            lastReceiveEventTalker = this.currentTalker;
            receiveEventGeneration++;
        }
        if (changed) {
            Log.d(TAG, "Qt service - RX state updated: " + this.isReceiving + " talker=" + this.currentTalker);
        }
    }

    public void updateTransmitState(boolean transmitting) {
        boolean previousTransmitting = this.isTransmitting;
        boolean previousReceiving = this.isReceiving;
        this.isTransmitting = transmitting;
        if (transmitting) {
            this.isReceiving = false;
            upgradeForegroundForMicrophone();
        }
        if (mediaSessionManager != null) {
            mediaSessionManager.updateTXStatus(transmitting);
        }
        syncForegroundState();
        if (previousTransmitting != this.isTransmitting || previousReceiving != this.isReceiving) {
            Log.d(TAG, "Qt service - TX state updated: " + this.isTransmitting);
        }
    }

    void dispatchNetworkStateForTesting(int generation,
                                        int reason,
                                        boolean hasDefaultNetwork,
                                        boolean validated,
                                        int transport,
                                        boolean metered,
                                        boolean captivePortal,
                                        boolean routeChanged) {
        dispatchNetworkStateChangedCallback(new NetworkHandoverMonitor.NetworkSnapshot(
                generation,
                reason,
                hasDefaultNetwork,
                validated,
                transport,
                metered,
                captivePortal,
                routeChanged,
                hasDefaultNetwork ? generation : -1L,
                ""));
    }
    
    private void syncForegroundState() {
        if (!isServiceRunning) {
            return;
        }

        if (VoipServiceLifecyclePolicy.shouldRemainForeground(
                isConnected,
                isReceiving,
                isTransmitting,
                LatryActivity.isActivityVisible())) {
            Notification notification = createNotification();
            if (!foregroundStarted) {
                startForegroundWithType(NOTIFICATION_ID, notification);
                foregroundStarted = true;
            } else {
                publishNotification(notification);
            }
            return;
        }

        if (foregroundStarted) {
            stopForegroundCompat();
            foregroundStarted = false;
            cancelActiveNotification();
        }
    }

    private void publishNotification(Notification notification) {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, notification);
        }
    }

    private void cancelActiveNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.cancel(NOTIFICATION_ID);
        }
    }

    private void registerBackgroundMeigPttReceiver() {
        if (backgroundMeigPttReceiver != null) {
            return;
        }

        backgroundMeigPttReceiver = new PTTButtonBroadcastReceiver();
        IntentFilter filter = new IntentFilter(PTTButtonBroadcastReceiver.ACTION_MEIG_KEY_EVENT);
        filter.setPriority(999);
        ContextCompat.registerReceiver(
                this,
                backgroundMeigPttReceiver,
                filter,
                ContextCompat.RECEIVER_EXPORTED);
        Log.d(TAG, "Background Meig PTT receiver registered");
    }

    private void unregisterBackgroundMeigPttReceiver() {
        if (backgroundMeigPttReceiver == null) {
            return;
        }

        try {
            unregisterReceiver(backgroundMeigPttReceiver);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "Background Meig PTT receiver already unregistered", e);
        }
        backgroundMeigPttReceiver = null;
    }
    
    // Notification action handlers removed - keeping notification simple
    
    @Override
    public IBinder onBind(Intent intent) {
        Log.d(TAG, "VoIP service onBind called");
        return null;
    }
    
    @Override
    public void onDestroy() {
        Log.d(TAG, "Qt VoIP Background Service destroyed");
        LatrySentry.addBreadcrumb("service.lifecycle", "service_destroyed",
                io.sentry.SentryLevel.INFO);
        unregisterBackgroundMeigPttReceiver();
        stopKeepAlive();
        stopNetworkMonitor();
        if (foregroundStarted) {
            stopForegroundCompat();
            foregroundStarted = false;
        }
        releaseSystemLocks();
        if (mediaSessionManager != null) {
            mediaSessionManager.release();
            mediaSessionManager = null;
        }
        instance = null;
        isServiceRunning = false;
        if (qtServiceAttached) {
            super.onDestroy();
        }
    }

    private void handleAndroidControlEvent(int eventType) {
        Log.d(TAG, "Forwarding Android control event to Qt: " + eventType);
        dispatchAndroidControlEventCallback(eventType);
    }
    
    // Static methods for Qt application integration
    public static VoipBackgroundService getInstance() {
        return instance;
    }

    private void dispatchServiceStartedCallback() {
        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordServiceStarted();
            return;
        }
        invokeNativeCallback("notifyServiceStarted", new NativeCallback() {
            @Override
            public void run() {
                notifyServiceStarted();
            }
        });
    }

    private void dispatchServiceStoppedCallback() {
        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordServiceStopped();
            return;
        }
        invokeNativeCallback("notifyServiceStopped", new NativeCallback() {
            @Override
            public void run() {
                notifyServiceStopped();
            }
        });
    }

    private void dispatchCheckConnectionCallback() {
        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordCheckConnection();
            return;
        }
        invokeNativeCallback("notifyCheckConnection", new NativeCallback() {
            @Override
            public void run() {
                notifyCheckConnection();
            }
        });
    }

    private void dispatchAndroidControlEventCallback(int eventType) {
        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordControlEvent(eventType);
            return;
        }
        final int callbackEventType = eventType;
        invokeNativeCallback("notifyAndroidControlEvent", new NativeCallback() {
            @Override
            public void run() {
                notifyAndroidControlEvent(callbackEventType);
            }
        });
    }

    private void handleNetworkSnapshot(NetworkHandoverMonitor.NetworkSnapshot snapshot) {
        if (snapshot == null) {
            return;
        }

        Log.d(TAG, "Default network snapshot: " + snapshot);
        lastDefaultNetworkGeneration = snapshot.generation;
        lastDefaultNetworkTransport = snapshot.transport;
        dispatchNetworkStateChangedCallback(snapshot);
    }

    private void dispatchNetworkStateChangedCallback(NetworkHandoverMonitor.NetworkSnapshot snapshot) {
        if (snapshot == null) {
            return;
        }

        if (AndroidIntegrationTestHooks.isEnabled()) {
            AndroidIntegrationTestHooks.recordNetworkStateChanged(
                    snapshot.generation,
                    snapshot.reason,
                    snapshot.hasDefaultNetwork,
                    snapshot.validated,
                    snapshot.transport,
                    snapshot.metered,
                    snapshot.captivePortal,
                    snapshot.routeChanged);
            return;
        }

        final int generation = snapshot.generation;
        final int reason = snapshot.reason;
        final boolean hasDefaultNetwork = snapshot.hasDefaultNetwork;
        final boolean validated = snapshot.validated;
        final int transport = snapshot.transport;
        final boolean metered = snapshot.metered;
        final boolean captivePortal = snapshot.captivePortal;
        final boolean routeChanged = snapshot.routeChanged;
        invokeNativeCallback("notifyNetworkStateChanged", new NativeCallback() {
            @Override
            public void run() {
                notifyNetworkStateChanged(generation, reason, hasDefaultNetwork, validated,
                        transport, metered, captivePortal, routeChanged);
            }
        });
    }

    private void invokeNativeCallback(String callbackName, NativeCallback callback) {
        try {
            callback.run();
            if (!nativeBridgeAvailable) {
                nativeBridgeAvailable = true;
                Log.i(TAG, "Native bridge restored for " + callbackName);
            }
        } catch (UnsatisfiedLinkError error) {
            if (nativeBridgeAvailable) {
                nativeBridgeAvailable = false;
                Log.w(TAG, "Native bridge unavailable for " + callbackName
                        + "; Java foreground controller will continue until Qt is ready");
            }
        }
    }

    private interface NativeCallback {
        void run();
    }

    private boolean isQtRuntimeStarted() {
        try {
            Class<?> qtNativeClass = Class.forName("org.qtproject.qt.android.QtNative");
            Method getStateDetails = qtNativeClass.getDeclaredMethod("getStateDetails");
            getStateDetails.setAccessible(true);
            Object stateDetails = getStateDetails.invoke(null);
            if (stateDetails == null) {
                return false;
            }

            Field isStartedField = stateDetails.getClass().getDeclaredField("isStarted");
            isStartedField.setAccessible(true);
            return isStartedField.getBoolean(stateDetails);
        } catch (Exception e) {
            Log.w(TAG, "Failed to inspect Qt runtime state; defaulting to Qt service attach", e);
            return false;
        }
    }

    /**
     * Detects whether QtLoader.m_instance has already been claimed by the Activity's
     * QtActivityLoader.  This covers the timing gap where the Activity has created
     * its loader (setting m_instance) but QtNative.startApplication() has not yet
     * run (deferred to ViewTreeObserver.OnGlobalLayoutListener), so isStarted is
     * still false.  Calling super.onCreate() in this state triggers a ClassCastException
     * because QtServiceLoader.getServiceLoader() tries to downcast the existing
     * QtActivityLoader to QtServiceLoader.
     */
    private boolean isQtLoaderConflict() {
        try {
            Class<?> loaderClass = Class.forName("org.qtproject.qt.android.QtLoader");
            Field instanceField = loaderClass.getDeclaredField("m_instance");
            instanceField.setAccessible(true);
            Object instance = instanceField.get(null);
            if (instance == null) {
                return false;
            }
            boolean conflict = !instance.getClass().getName().contains("ServiceLoader");
            if (conflict) {
                Log.d(TAG, "QtLoader.m_instance is " + instance.getClass().getName()
                        + " — Activity loader present before isStarted; avoiding QtServiceLoader conflict");
            }
            return conflict;
        } catch (Exception e) {
            Log.w(TAG, "Failed to inspect QtLoader.m_instance; assuming no conflict", e);
            return false;
        }
    }
    
    // Qt-Native bridge methods - called from C++
    private static native void notifyServiceStarted();
    private static native void notifyServiceStopped();
    private static native void notifyCheckConnection();
    private static native void notifyAndroidControlEvent(int eventType);
    private static native void notifyNetworkStateChanged(int generation,
                                                         int reason,
                                                         boolean hasDefaultNetwork,
                                                         boolean validated,
                                                         int transport,
                                                         boolean metered,
                                                         boolean captivePortal,
                                                         boolean routeChanged);
}
