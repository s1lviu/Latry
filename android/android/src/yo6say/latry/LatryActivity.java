package yo6say.latry;

import android.os.Bundle;
import android.view.WindowManager;
import android.media.AudioManager;
import android.media.AudioFocusRequest;
import android.media.AudioAttributes;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.PowerManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.provider.Settings;
import android.system.Os;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyCallback;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.view.KeyEvent;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;
import androidx.core.content.ContextCompat;
import org.qtproject.qt.android.bindings.QtActivity;

public class LatryActivity extends QtActivity {
    private static final String TAG = "LatryActivity";
    private static final int OPTIONAL_PERMISSIONS_REQUEST_CODE = 1001;
    private static final int FOCUS_MODE_NONE = 0;
    private static final int FOCUS_MODE_RX = 1;
    private static final int FOCUS_MODE_TX = 2;
    private static final long[] TOT_WARNING_VIBRATION_PATTERN_MS = new long[] {0L, 80L, 60L, 120L};

    private static AudioManager audioManager;
    private static AudioManager.OnAudioFocusChangeListener focusChangeListener;
    private static AudioFocusRequest audioFocusRequest;
    private static PowerManager.WakeLock wakeLock;
    private static WifiManager.WifiLock wifiLock;
    private static boolean audioFocusRequested = false;
    private static int currentFocusMode = FOCUS_MODE_NONE;
    private static volatile boolean appInitiatedShutdownRequested = false;
    private static volatile boolean activityVisible = false;
    private static volatile LatryActivity currentActivityInstance = null;
    private static TelephonyManager telephonyManager;
    private static Object telephonyCallback;
    @SuppressWarnings("deprecation")
    private static PhoneStateListener phoneStateListener;
    private PTTButtonBroadcastReceiver dynamicPttReceiver;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        applyQtAccessibilityCrashWorkaroundIfNeeded();
        super.onCreate(savedInstanceState);
        appInitiatedShutdownRequested = false;
        currentActivityInstance = this;
        LatrySentry.addBreadcrumb("ui.lifecycle", "activity_created", io.sentry.SentryLevel.INFO);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        focusChangeListener = new AudioManager.OnAudioFocusChangeListener() {
            @Override
            public void onAudioFocusChange(int focusChange) {
                Log.d(TAG, "Audio focus changed: " + focusChange);
                switch (focusChange) {
                    case AudioManager.AUDIOFOCUS_LOSS:
                        Log.d(TAG, "Audio focus lost permanently");
                        notifyAudioFocusLost();
                        LatryActivity.abandonAudioFocus();   // releases wake-lock
                        break;
                    case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                        Log.d(TAG, "Audio focus lost temporarily");
                        notifyAudioFocusPaused();
                        break;
                    case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                        Log.d(TAG, "Audio focus lost temporarily - can duck");
                        notifyAudioFocusPaused();
                        break;
                    case AudioManager.AUDIOFOCUS_GAIN:
                        Log.d(TAG, "Audio focus gained");
                        renewLocks(); // Renew locks on focus gain
                        notifyAudioFocusGained();
                        break;
                    case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK:
                        Log.d(TAG, "Audio focus gained transiently - may duck");
                        renewLocks(); // Renew locks on focus gain
                        notifyAudioFocusGained();
                        break;
                    default:
                        Log.d(TAG, "Audio focus: unhandled code " + focusChange);
                        break;
                }
            }
        };

        // Build initial focus request as RX (TRANSIENT mixing)
        buildFocusRequest(FOCUS_MODE_RX);

        telephonyManager = (TelephonyManager) getSystemService(Context.TELEPHONY_SERVICE);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Latry:Audio");
            wakeLock.setReferenceCounted(false);
        }

        // Initialize WiFi lock for better connectivity during calls
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm != null) {
            wifiLock = createWifiLockCompat(wm, "Latry:Wifi");
            wifiLock.setReferenceCounted(false);
        }
        
        if (!AndroidIntegrationTestHooks.isEnabled()) {
            // Request battery optimization exemption for VoIP functionality
            requestBatteryOptimizationExemption();

            requestOptionalRuntimePermissions();
        } else if (hasReadPhoneStatePermission()) {
            startPhoneCallMonitoring();
        }

        registerDynamicPttReceiver();
    }

    private static void applyQtAccessibilityCrashWorkaroundIfNeeded() {
        if (!shouldDisableQtAndroidAccessibility(Build.MANUFACTURER, Build.BRAND)) {
            return;
        }

        try {
            Os.setenv("QT_ANDROID_DISABLE_ACCESSIBILITY", "1", true);
            Log.i(TAG, "Disabled Qt Android accessibility for manufacturer workaround: "
                    + Build.MANUFACTURER + "/" + Build.BRAND);
        } catch (Exception e) {
            Log.w(TAG, "Unable to disable Qt Android accessibility workaround", e);
        }
    }

    static boolean shouldDisableQtAndroidAccessibility(String manufacturer, String brand) {
        return isMatchingAndroidVendor(manufacturer, "Huawei")
                || isMatchingAndroidVendor(manufacturer, "Honor")
                || isMatchingAndroidVendor(brand, "Huawei")
                || isMatchingAndroidVendor(brand, "Honor");
    }

    private static boolean isMatchingAndroidVendor(String value, String expected) {
        return value != null && value.equalsIgnoreCase(expected);
    }

    private void registerDynamicPttReceiver() {
        if (dynamicPttReceiver != null) {
            return;
        }

        dynamicPttReceiver = new PTTButtonBroadcastReceiver();
        IntentFilter filter = new IntentFilter();
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT_UP);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT_KEY);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN_ALT);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_PTT_UP_ALT);
        filter.addAction(PTTButtonBroadcastReceiver.ACTION_MEIG_KEY_EVENT);
        filter.setPriority(999);
        ContextCompat.registerReceiver(
                this,
                dynamicPttReceiver,
                filter,
                ContextCompat.RECEIVER_EXPORTED);
        Log.i(TAG, "Dynamic PTT broadcast receiver registered");
    }

    public static void requestAudioFocus() {
        // Resume/recovery callbacks must preserve TX focus if a transmit session is already active.
        requestAudioFocusInternal(resolveRequestedFocusMode());
    }

    public static void requestAudioFocusForRX() {
        requestAudioFocusInternal(FOCUS_MODE_RX);
    }

    public static void requestAudioFocusForTX() {
        requestAudioFocusInternal(FOCUS_MODE_TX);
    }

    private static void requestAudioFocusInternal(int mode) {
        if (audioManager == null) {
            return;
        }

        // If focus mode changed, release old focus first and rebuild request
        if (audioFocusRequested && currentFocusMode != mode) {
            Log.i(TAG, "Focus mode changing from " + focusModeName(currentFocusMode)
                    + " to " + focusModeName(mode) + ", re-requesting");
            abandonAudioFocusInternal();
        }

        if (!audioFocusRequested) {
            buildFocusRequest(mode);
            int result = audioManager.requestAudioFocus(audioFocusRequest);
            audioFocusRequested = (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
            currentFocusMode = mode;
            Log.i(TAG, "Audio focus request (" + focusModeName(mode) + ") result: " + result);
        }

        // acquire locks when we own focus
        if (audioFocusRequested && wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
        }
        if (audioFocusRequested && wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
        }
    }

    public static void abandonAudioFocus() {
        abandonAudioFocusInternal();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
        }
    }

    private static void abandonAudioFocusInternal() {
        if (audioManager != null && audioFocusRequested) {
            if (audioFocusRequest != null) {
                audioManager.abandonAudioFocusRequest(audioFocusRequest);
            }
            audioFocusRequested = false;
            currentFocusMode = FOCUS_MODE_NONE;
            Log.d(TAG, "Audio focus abandoned");
        }
    }

    private static void buildFocusRequest(int mode) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }

        AudioAttributes attributes;
        int focusGain;
        boolean willPauseWhenDucked;

        if (mode == FOCUS_MODE_TX) {
            // TX: exclusive focus, voice communication attributes
            attributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build();
            focusGain = AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE;
            willPauseWhenDucked = true;
        } else {
            // RX: transient focus, media attributes (allows mixing)
            attributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build();
            focusGain = AudioManager.AUDIOFOCUS_GAIN_TRANSIENT;
            willPauseWhenDucked = false;
        }

        audioFocusRequest = new AudioFocusRequest.Builder(focusGain)
                .setAudioAttributes(attributes)
                .setAcceptsDelayedFocusGain(false)
                .setWillPauseWhenDucked(willPauseWhenDucked)
                .setOnAudioFocusChangeListener(focusChangeListener)
                .build();
    }

    public static void setAudioMode(int mode) {
        if (audioManager != null) {
            audioManager.setMode(mode);
            Log.i(TAG, "Audio mode set to " + mode);
        }
    }

    public static void renewLocks() {
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
        }
        if (wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
        }
    }

    private static WifiManager.WifiLock createWifiLockCompat(WifiManager wifiManager, String tag) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, tag);
        }
        return createLegacyWifiLock(wifiManager, tag);
    }

    @SuppressWarnings("deprecation")
    private static WifiManager.WifiLock createLegacyWifiLock(WifiManager wifiManager, String tag) {
        return wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, tag);
    }

    @Override
    protected void onPause() {
        super.onPause();
        activityVisible = false;
        Log.d(TAG, "Activity paused");
        // Notify C++ about activity pause for PTT release
        notifyActivityPaused();
        // Don't abandon audio focus here - let the app decide
    }

    @Override
    protected void onResume() {
        super.onResume();
        activityVisible = true;
        Log.d(TAG, "Activity resumed");
        // Delay C++ notification to ensure Qt is fully initialized
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                notifyActivityResumed();
            }
        }, 200); // Small delay for Qt initialization
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event == null) {
            return super.dispatchKeyEvent(event);
        }

        final int keyCode = event.getKeyCode();
        final int action = event.getAction();

        // Log all non-trivial key events for PTT diagnostics
        if (HardwarePttKeyPolicy.isLearnableHardwareKeyCode(keyCode)) {
            final int learnedKey = HardwarePttSettingsStore.getLearnedPttKeyCode(this);
            Log.i(TAG, "dispatchKeyEvent: keyCode=" + KeyEvent.keyCodeToString(keyCode)
                    + "(" + keyCode + ") action=" + action
                    + " learnedKey=" + learnedKey
                    + " alwaysSupported=" + HardwarePttKeyPolicy.isAlwaysSupportedKeyCode(keyCode)
                    + " pocEnabled=" + HardwarePttSettingsStore.isPocButtonEnabled(this));
        }

        // Learning mode intercepts keys before normal PTT dispatch
        if (HardwarePttLearningCoordinator.isLearningActive()) {
            if (HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                    this, action, keyCode)) {
                Log.i(TAG, "Key event consumed by PTT learning: keyCode="
                        + KeyEvent.keyCodeToString(keyCode)
                        + " action=" + action);
                return true;
            }
        }

        if (HardwarePttKeyPolicy.shouldHandleKeyEvent(
                this,
                Build.BRAND,
                Build.MANUFACTURER,
                Build.MODEL,
                Build.DEVICE,
                Build.PRODUCT,
                action,
                keyCode)) {
            final boolean pressed = action == KeyEvent.ACTION_DOWN;
            Log.i(TAG, "Hardware PTT key accepted: keyCode="
                    + KeyEvent.keyCodeToString(keyCode)
                    + "(" + keyCode + ") action=" + action);
            if (pressed) {
                autoLearnDetectedPttKeyCode(keyCode);
            }
            VoipBackgroundService.dispatchPTTAction(this, pressed);
            return true;
        }

        return super.dispatchKeyEvent(event);
    }

    static boolean isActivityVisible() {
        return activityVisible;
    }

    static LatryActivity currentActivityInstanceForTesting() {
        return currentActivityInstance;
    }

    static String currentFocusModeNameForTesting() {
        return focusModeName(currentFocusMode);
    }

    static String requestedFocusModeNameForTesting() {
        return focusModeName(resolveRequestedFocusMode());
    }

    static boolean isAudioFocusRequestedForTesting() {
        return audioFocusRequested;
    }

    static boolean shouldForwardPermissionResultToQt(int requestCode) {
        return requestCode != OPTIONAL_PERMISSIONS_REQUEST_CODE;
    }

    private static int resolveRequestedFocusMode() {
        return currentFocusMode != FOCUS_MODE_NONE ? currentFocusMode : FOCUS_MODE_RX;
    }

    // Battery optimization handling for VoIP apps
    private void requestBatteryOptimizationExemption() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null && !pm.isIgnoringBatteryOptimizations(getPackageName())) {
                Log.d(TAG, "Requesting battery optimization exemption for VoIP functionality");
                Intent intent = new Intent();
                intent.setAction(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                try {
                    startActivity(intent);
                } catch (Exception e) {
                    Log.w(TAG, "Could not open battery optimization settings", e);
                    // Fallback to general battery optimization settings
                    try {
                        Intent fallbackIntent = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
                        startActivity(fallbackIntent);
                    } catch (Exception e2) {
                        Log.w(TAG, "Could not open battery optimization settings at all", e2);
                    }
                }
            } else {
                Log.d(TAG, "Battery optimization is already disabled for this app");
            }
        }
    }
    
    public static boolean isBatteryOptimizationDisabled(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            return pm != null && pm.isIgnoringBatteryOptimizations(context.getPackageName());
        }
        return true; // Not applicable on older versions
    }
    
    private void requestOptionalRuntimePermissions() {
        List<String> missingPermissions = new ArrayList<>();

        if (!hasReadPhoneStatePermission() && shouldRequestPhoneStatePermission()) {
            missingPermissions.add(android.Manifest.permission.READ_PHONE_STATE);
            Log.d(TAG, "Requesting READ_PHONE_STATE for optional phone call monitoring");
        } else if (hasReadPhoneStatePermission()) {
            startPhoneCallMonitoring();
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                missingPermissions.add(android.Manifest.permission.POST_NOTIFICATIONS);
                Log.d(TAG, "Requesting POST_NOTIFICATIONS permission for foreground service");
            } else {
                Log.d(TAG, "POST_NOTIFICATIONS permission already granted");
            }
        } else {
            Log.d(TAG, "POST_NOTIFICATIONS permission not required on this Android version");
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                && getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH)
                && checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
            missingPermissions.add(android.Manifest.permission.BLUETOOTH_CONNECT);
            Log.d(TAG, "Requesting BLUETOOTH_CONNECT for headset audio routing");
        }

        if (missingPermissions.isEmpty()) {
            return;
        }

        requestPermissions(missingPermissions.toArray(new String[0]), OPTIONAL_PERMISSIONS_REQUEST_CODE);
    }

    private boolean shouldRequestPhoneStatePermission() {
        return telephonyManager != null
                && getPackageManager().hasSystemFeature(PackageManager.FEATURE_TELEPHONY);
    }

    private boolean hasReadPhoneStatePermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }

        return checkSelfPermission(android.Manifest.permission.READ_PHONE_STATE)
                == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        // QtActivityBase forwards every permission result into QtNative's pending-request dispatcher.
        // Keep app-owned request codes local so Qt only sees results for requests it initiated itself.
        if (shouldForwardPermissionResultToQt(requestCode)) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        for (int i = 0; i < permissions.length; ++i) {
            boolean granted = i < grantResults.length
                    && grantResults[i] == PackageManager.PERMISSION_GRANTED;
            String permission = permissions[i];
            Log.i(TAG, "Runtime permission result " + permission + "=" + granted);

            if (android.Manifest.permission.READ_PHONE_STATE.equals(permission) && granted) {
                startPhoneCallMonitoring();
            }
        }
    }

    // Public methods for C++ wake lock control
    public static void acquireWakeLock() {
        Log.d(TAG, "C++ requested wake lock acquisition");
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
            Log.d(TAG, "Wake lock acquired for background VoIP");
        }
        if (wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
            Log.d(TAG, "WiFi lock acquired for background VoIP");
        }
    }
    
    public static void releaseWakeLock() {
        Log.d(TAG, "C++ requested wake lock release");
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
            Log.d(TAG, "Wake lock released");
        }
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
            Log.d(TAG, "WiFi lock released");
        }
    }

    public static void vibrateTotWarning(Context context) {
        if (context == null) {
            Log.w(TAG, "Skipping TOT warning haptic: no context");
            return;
        }

        Vibrator vibrator = getVibrator(context);
        if (vibrator == null || !vibrator.hasVibrator()) {
            Log.d(TAG, "Skipping TOT warning haptic: vibrator unavailable");
            return;
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(
                        VibrationEffect.createWaveform(TOT_WARNING_VIBRATION_PATTERN_MS, -1));
            } else {
                vibrateTotWarningLegacy(vibrator);
            }
        } catch (SecurityException e) {
            Log.w(TAG, "TOT warning haptic blocked by permission", e);
        } catch (RuntimeException e) {
            Log.w(TAG, "TOT warning haptic failed", e);
        }
    }

    @SuppressWarnings("deprecation")
    private static Vibrator getVibrator(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibratorManager vibratorManager = context.getSystemService(VibratorManager.class);
            if (vibratorManager != null) {
                return vibratorManager.getDefaultVibrator();
            }
        }

        return (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
    }

    @SuppressWarnings("deprecation")
    private static void vibrateTotWarningLegacy(Vibrator vibrator) {
        vibrator.vibrate(TOT_WARNING_VIBRATION_PATTERN_MS, -1);
    }

    // Phone call monitoring — detect incoming/active calls and pause audio
    private static void startPhoneCallMonitoring() {
        if (telephonyManager == null) {
            Log.w(TAG, "TelephonyManager not available - phone call monitoring disabled");
            return;
        }

        if (telephonyCallback != null || phoneStateListener != null) {
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            startPhoneCallMonitoringApi31();
        } else {
            startPhoneCallMonitoringLegacy();
        }
    }

    private static void startPhoneCallMonitoringApi31() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return;
        }

        try {
            Executor mainExecutor = new HandlerExecutor(new Handler(Looper.getMainLooper()));
            CallStateCallback callback = new CallStateCallback();
            telephonyCallback = callback;
            telephonyManager.registerTelephonyCallback(mainExecutor, callback);
            Log.i(TAG, "Phone call monitoring enabled (API 31+)");
        } catch (SecurityException e) {
            telephonyCallback = null;
            Log.w(TAG, "Phone call monitoring disabled - READ_PHONE_STATE not granted");
        } catch (Exception e) {
            telephonyCallback = null;
            Log.w(TAG, "Phone call monitoring failed to start", e);
        }
    }

    @android.annotation.TargetApi(Build.VERSION_CODES.S)
    private static class CallStateCallback extends TelephonyCallback
            implements TelephonyCallback.CallStateListener {
        @Override
        public void onCallStateChanged(int state) {
            handlePhoneCallStateChange(state);
        }
    }

    @SuppressWarnings("deprecation")
    private static void startPhoneCallMonitoringLegacy() {
        try {
            phoneStateListener = new PhoneStateListener() {
                @Override
                public void onCallStateChanged(int state, String phoneNumber) {
                    handlePhoneCallStateChange(state);
                }
            };
            telephonyManager.listen(phoneStateListener, PhoneStateListener.LISTEN_CALL_STATE);
            Log.i(TAG, "Phone call monitoring enabled (Legacy API)");
        } catch (SecurityException e) {
            phoneStateListener = null;
            Log.w(TAG, "Phone call monitoring disabled - READ_PHONE_STATE not granted");
        } catch (Exception e) {
            phoneStateListener = null;
            Log.w(TAG, "Phone call monitoring failed to start", e);
        }
    }

    private static void handlePhoneCallStateChange(int state) {
        if (state == TelephonyManager.CALL_STATE_IDLE) {
            Log.i(TAG, "Phone call ended - audio system available");
        } else if (state == TelephonyManager.CALL_STATE_RINGING) {
            Log.w(TAG, "Phone call ringing - triggering audio focus loss");
            notifyAudioFocusLost();
        } else if (state == TelephonyManager.CALL_STATE_OFFHOOK) {
            Log.w(TAG, "Phone call active - triggering audio focus loss");
            notifyAudioFocusLost();
        }
    }

    private static String focusModeName(int mode) {
        switch (mode) {
            case FOCUS_MODE_RX: return "RX_MIXING";
            case FOCUS_MODE_TX: return "TX_EXCLUSIVE";
            default: return "NONE";
        }
    }

    private static class HandlerExecutor implements Executor {
        private final Handler handler;
        HandlerExecutor(Handler handler) { this.handler = handler; }
        @Override
        public void execute(Runnable command) { handler.post(command); }
    }

    static boolean shouldPrepareNativeShutdown(boolean finishing,
                                               boolean changingConfigurations,
                                               boolean serviceRunning) {
        return shouldPrepareNativeShutdown(finishing, changingConfigurations, serviceRunning, false);
    }

    static boolean shouldPrepareNativeShutdown(boolean finishing,
                                               boolean changingConfigurations,
                                               boolean serviceRunning,
                                               boolean appShutdownRequested) {
        return finishing && !changingConfigurations && !serviceRunning && !appShutdownRequested;
    }

    @Override
    protected void onDestroy() {
        if (currentActivityInstance == this) {
            currentActivityInstance = null;
        }
        unregisterDynamicPttReceiver();

        final boolean finishing = isFinishing();
        final boolean changingConfigurations = isChangingConfigurations();
        final boolean serviceRunning = VoipBackgroundService.isRunning();

        // Only run eager native shutdown prep when the activity itself is
        // actually finishing and no background VoIP service needs to outlive it.
        if (shouldPrepareNativeShutdown(finishing,
                                        changingConfigurations,
                                        serviceRunning,
                                        appInitiatedShutdownRequested)) {
            Log.d(TAG, "onDestroy: triggering native shutdown preparation for finishing activity");
            nativePrepareForShutdown();
        } else {
            Log.d(TAG, "onDestroy: skipping native shutdown preparation"
                    + " finishing=" + finishing
                    + " changingConfigurations=" + changingConfigurations
                    + " serviceRunning=" + serviceRunning
                    + " appInitiatedShutdownRequested=" + appInitiatedShutdownRequested);
        }
        super.onDestroy();
    }

    public static void requestFinishAndRemoveTask() {
        appInitiatedShutdownRequested = true;
        final LatryActivity activity = currentActivityInstance;
        if (activity == null) {
            Log.w(TAG, "requestFinishAndRemoveTask: no active activity instance");
            return;
        }

        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(() -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                activity.finishAndRemoveTask();
            } else {
                activity.finish();
            }
        });
    }

    private void unregisterDynamicPttReceiver() {
        if (dynamicPttReceiver == null) {
            return;
        }

        try {
            unregisterReceiver(dynamicPttReceiver);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "Dynamic PTT receiver already unregistered", e);
        }
        dynamicPttReceiver = null;
    }

    private void autoLearnDetectedPttKeyCode(int keyCode) {
        if (keyCode == HardwarePttSettingsStore.getLearnedPttKeyCode(this)) {
            return;
        }

        HardwarePttSettingsStore.setLearnedPttKeyCode(this, keyCode);
        Log.i(TAG, "Auto-learned detected PTT keyCode=" + keyCode);
        try {
            nativeNotifyAutoDetectedPttKeyCode(keyCode);
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Native bridge unavailable for auto-detected PTT key", e);
        }
    }

    // JNI methods to notify C++ about audio focus changes
    private static native void notifyAudioFocusLost();
    private static native void notifyAudioFocusPaused();
    private static native void notifyAudioFocusGained();
    private static native void notifyActivityPaused();
    private static native void notifyActivityResumed();
    private static native void nativePrepareForShutdown();

    // Called by HardwarePttLearningCoordinator when learning ends
    static native void notifyHardwarePttLearningResult(int result, int keyCode);

    // Called when dispatchKeyEvent auto-detects a PTT keycode
    private static native void nativeNotifyAutoDetectedPttKeyCode(int keyCode);
}
