package yo6say.latry;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
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
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import android.app.Service;

public class VoipBackgroundService extends Service {
    private static final String TAG = "VoipBackgroundService";
    private static final int NOTIFICATION_ID = 1001;
    private static final String CHANNEL_ID = "voip_service_channel";
    private static final String CHANNEL_NAME = "VoIP Background Service";
    
    // Service state
    private static boolean isServiceRunning = false;
    private static VoipBackgroundService instance = null;
    
    // Connection state
    private String serverHost = "";
    private int serverPort = 0;
    private int talkGroup = 0;
    private String callsign = "";
    private String connectionStatus = "Connecting...";
    private String currentTalker = "";
    private boolean isConnected = false;
    private boolean isMuted = false;
    
    // System locks
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private AudioManager audioManager;
    
    // Keep-alive mechanism
    private Handler keepAliveHandler;
    private Runnable keepAliveRunnable;
    
    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "VoIP Background Service created");
        instance = this;
        
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
            wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "Latry:VoipService");
            wifiLock.setReferenceCounted(false);
        }
        
        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        
        // Create notification channel for Android O+
        createNotificationChannel();
    }
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "Qt VoIP Service onStartCommand");
        
        if (intent != null) {
            String action = intent.getAction();
            
            if ("START_VOIP".equals(action)) {
                // Extract connection parameters
                serverHost = intent.getStringExtra("host");
                serverPort = intent.getIntExtra("port", 0);
                talkGroup = intent.getIntExtra("talkgroup", 0);
                callsign = intent.getStringExtra("callsign");
                connectionStatus = "Connecting to " + serverHost + ":" + serverPort;
                
                startVoipService();
                
            } else if ("STOP_VOIP".equals(action)) {
                stopVoipService();
                return START_NOT_STICKY;
                
            }
        }
        
        return START_STICKY; // Restart if killed by system
    }
    
    private void startVoipService() {
        Log.d(TAG, "Starting Qt VoIP foreground service");
        isServiceRunning = true;
        
        // Check battery optimization status
        checkBatteryOptimization();
        
        // Acquire system locks
        acquireSystemLocks();
        
        // Start foreground with initial notification
        Notification notification = createNotification();
        startForeground(NOTIFICATION_ID, notification);
        
        // Start keep-alive mechanism
        startKeepAlive();
        
        // Notify Qt application that service is ready
        notifyServiceStarted();
    }
    
    private void stopVoipService() {
        Log.d(TAG, "Stopping Qt VoIP service");
        isServiceRunning = false;
        isConnected = false;
        
        // Stop keep-alive mechanism
        stopKeepAlive();
        
        // Release system locks
        releaseSystemLocks();
        
        // Notify Qt application that service is stopping
        notifyServiceStopped();
        
        // Stop foreground service
        stopForeground(true);
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
        if (keepAliveHandler == null) {
            keepAliveHandler = new Handler(Looper.getMainLooper());
        }
        
        keepAliveRunnable = new Runnable() {
            @Override
            public void run() {
                if (isServiceRunning) {
                    // Renew wake lock if needed
                    if (wakeLock != null && !wakeLock.isHeld()) {
                        wakeLock.acquire(10*60*1000L); // 10 minutes timeout
                        Log.d(TAG, "Wake lock renewed during freeze/unfreeze cycle");
                    }
                    
                    // Trigger Qt reconnection check for TCP disconnection (with delay for Qt readiness)
                    keepAliveHandler.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            notifyCheckConnection();
                        }
                    }, 100); // Small delay to ensure Qt is ready
                    
                    // Update notification to show we're still active
                    updateNotification();
                    
                    // Log keep-alive to show service is active
                    Log.d(TAG, "Qt VoIP service keep-alive - connection check triggered");
                    
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
    
    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                CHANNEL_NAME,
                NotificationManager.IMPORTANCE_DEFAULT
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
            .setContentTitle("Latry VoIP Active")
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
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE);
        
        // No action buttons - keep notification simple
        
        // Set expanded style
        NotificationCompat.BigTextStyle bigTextStyle = new NotificationCompat.BigTextStyle()
            .setBigContentTitle("Latry App Service")
            .bigText(buildDetailedNotificationText());
        builder.setStyle(bigTextStyle);
        
        return builder.build();
    }
    
    private String buildNotificationText() {
        return isConnected ? "Connected" : "Disconnected";
    }
    
    private String buildDetailedNotificationText() {
        return isConnected ? "Connected" : "Disconnected";
    }
    
    // Qt-compliant static service startup method (per Qt documentation)
    public static void startQtVoipService(Context context) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
        Log.d(TAG, "Qt VoIP service started via Qt-compliant static method");
    }
    
    // Backward compatibility method with parameters
    public static void startVoipService(Context context, String host, int port, String callsign, int talkgroup) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        intent.setAction("START_VOIP");
        intent.putExtra("host", host);
        intent.putExtra("port", port);
        intent.putExtra("callsign", callsign);
        intent.putExtra("talkgroup", talkgroup);
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
        Log.d(TAG, "VoIP service start requested with parameters");
    }
    
    public static void stopVoipService(Context context) {
        Intent intent = new Intent(context, VoipBackgroundService.class);
        intent.setAction("STOP_VOIP");
        context.startService(intent);
        Log.d(TAG, "VoIP service stop requested");
    }
    
    public static boolean isRunning() {
        return isServiceRunning;
    }
    
    // Qt-compliant update methods
    public void updateConnectionStatus(String status, boolean connected) {
        this.connectionStatus = status;
        this.isConnected = connected;
        updateNotification();
        Log.d(TAG, "Qt service - Connection status updated: " + status);
    }
    
    public void updateCurrentTalker(String talker) {
        this.currentTalker = talker;
        updateNotification();
        Log.d(TAG, "Qt service - Current talker updated: " + talker);
    }
    
    private void updateNotification() {
        if (isServiceRunning) {
            Notification notification = createNotification();
            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.notify(NOTIFICATION_ID, notification);
            }
        }
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
        releaseSystemLocks();
        instance = null;
        isServiceRunning = false;
        super.onDestroy();
    }
    
    // Static methods for Qt application integration
    public static VoipBackgroundService getInstance() {
        return instance;
    }
    
    // Qt-Native bridge methods - called from C++
    private static native void notifyServiceStarted();
    private static native void notifyServiceStopped();
    private static native void notifyCheckConnection();
}
