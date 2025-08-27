package yo6say.latry;

import android.os.Bundle;
import android.view.WindowManager;
import android.media.AudioManager;
import android.media.AudioFocusRequest;
import android.media.AudioAttributes;
import android.content.Context;
import android.content.Intent;
import android.os.PowerManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.provider.Settings;
import android.util.Log;
import org.qtproject.qt.android.bindings.QtActivity;

public class LatryActivity extends QtActivity {
    private static final String TAG = "LatryActivity";
    private static AudioManager audioManager;
    private static AudioManager.OnAudioFocusChangeListener focusChangeListener;
    private static AudioFocusRequest audioFocusRequest;
    private static AudioAttributes audioAttributes;
    private static PowerManager.WakeLock wakeLock;
    private static WifiManager.WifiLock wifiLock;
    private static boolean audioFocusRequested = false;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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

        // Create audio attributes for voice communication
        audioAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build();
        // Create AudioFocusRequest for Android 8.0+ (API level 26+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // SECURE PTT MODE: Use EXCLUSIVE focus to prevent other audio mixing during transmission
            audioFocusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE)
                    .setAudioAttributes(audioAttributes)
                    .setAcceptsDelayedFocusGain(false)
                    // Ensure exclusive audio access during PTT for radio security
                    .setWillPauseWhenDucked(false)  // We want exclusive, not ducking during PTT
                    .setOnAudioFocusChangeListener(focusChangeListener)
                    .build();
        }

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Latry:Audio");
            wakeLock.setReferenceCounted(false);
        }

        // Initialize WiFi lock for better connectivity during calls
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wm != null) {
            wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "Latry:Wifi");
            wifiLock.setReferenceCounted(false);
        }
        
        // Request battery optimization exemption for VoIP functionality
        requestBatteryOptimizationExemption();
        
        // Request notification permission for Android 13+ (required for foreground service notifications)
        requestNotificationPermission();
    }

    public static void requestAudioFocus() {
        requestSecureAudioFocus(); // Default to secure mode
    }
    
    // SECURE MODE: Exclusive audio focus for PTT transmission (prevents other audio mixing)
    public static void requestSecureAudioFocus() {
        if (audioManager != null && !audioFocusRequested) {
            int result;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                // Use EXCLUSIVE focus to prevent other apps' audio from mixing during PTT
                result = audioManager.requestAudioFocus(audioFocusRequest);
            } else {
                // Fallback for older Android versions (API 21-25)
                result = audioManager.requestAudioFocus(focusChangeListener,
                        AudioManager.STREAM_VOICE_CALL,
                        AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE);
            }
            audioFocusRequested = (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
            Log.d(TAG, "Secure audio focus request result: " + result + " (EXCLUSIVE mode for PTT)");
        }
        // acquire only when we really own focus
        if (audioFocusRequested && wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();    // add a timeout if you like
        }
        if (audioFocusRequested && wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
        }
    }
    
    // COOPERATIVE MODE: Shared audio focus for RX/idle (allows other apps to play with ducking)
    public static void requestCooperativeAudioFocus() {
        if (audioManager != null && !audioFocusRequested) {
            int result;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                // Use GAIN focus that allows ducking for cooperative behavior during RX
                AudioFocusRequest cooperativeRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                        .setAudioAttributes(audioAttributes)
                        .setAcceptsDelayedFocusGain(true)  // Allow delayed focus for cooperation
                        .setWillPauseWhenDucked(false)     // We can handle ducking
                        .setOnAudioFocusChangeListener(focusChangeListener)
                        .build();
                result = audioManager.requestAudioFocus(cooperativeRequest);
            } else {
                // Fallback: Use GAIN instead of EXCLUSIVE for cooperative behavior
                result = audioManager.requestAudioFocus(focusChangeListener,
                        AudioManager.STREAM_VOICE_CALL,
                        AudioManager.AUDIOFOCUS_GAIN);
            }
            audioFocusRequested = (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
            Log.d(TAG, "Cooperative audio focus request result: " + result + " (SHARED mode for RX)");
        }
        // For cooperative mode, we still acquire locks but allow audio sharing
        if (audioFocusRequested && wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
        }
        if (audioFocusRequested && wifiLock != null && !wifiLock.isHeld()) {
            wifiLock.acquire();
        }
    }

    public static void abandonAudioFocus() {
        if (audioManager != null && audioFocusRequested) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                // Use modern AudioFocusRequest for Android 8.0+
                audioManager.abandonAudioFocusRequest(audioFocusRequest);
            } else {
                // Fallback for older Android versions (API 21-25)
                audioManager.abandonAudioFocus(focusChangeListener);
            }
            audioFocusRequested = false;
            Log.d(TAG, "Audio focus abandoned");
        }
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
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

    @Override
    protected void onPause() {
        super.onPause();
        Log.d(TAG, "Activity paused");
        // Notify C++ about activity pause for PTT release
        notifyActivityPaused();
        // Don't abandon audio focus here - let the app decide
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "Activity resumed");
        // Delay C++ notification to ensure Qt is fully initialized
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                notifyActivityResumed();
            }
        }, 200); // Small delay for Qt initialization
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
    
    // Notification permission handling for Android 13+ (required for foreground service notifications)
    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) { // Android 13+
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != 
                android.content.pm.PackageManager.PERMISSION_GRANTED) {
                
                Log.d(TAG, "Requesting POST_NOTIFICATIONS permission for foreground service");
                requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 1001);
            } else {
                Log.d(TAG, "POST_NOTIFICATIONS permission already granted");
            }
        } else {
            Log.d(TAG, "POST_NOTIFICATIONS permission not required on this Android version");
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

    // JNI methods to notify C++ about audio focus changes
    private static native void notifyAudioFocusLost();
    private static native void notifyAudioFocusPaused();
    private static native void notifyAudioFocusGained();
    private static native void notifyActivityPaused();
    private static native void notifyActivityResumed();
}
