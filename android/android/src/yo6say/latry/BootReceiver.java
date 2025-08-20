package yo6say.latry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "BootReceiver";
    private static final String PREFS_NAME = "LatryVoipPrefs";
    private static final String PREF_WAS_CONNECTED = "was_connected";
    private static final String PREF_HOST = "last_host";
    private static final String PREF_PORT = "last_port";
    private static final String PREF_CALLSIGN = "last_callsign";
    private static final String PREF_TALKGROUP = "last_talkgroup";
    
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        Log.d(TAG, "Boot receiver got action: " + action);
        
        if (Intent.ACTION_BOOT_COMPLETED.equals(action) || 
            "android.intent.action.QUICKBOOT_POWERON".equals(action)) {
            
            // Check if we were connected before reboot/crash
            SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
            boolean wasConnected = prefs.getBoolean(PREF_WAS_CONNECTED, false);
            
            if (wasConnected) {
                Log.d(TAG, "App was connected before reboot - attempting to restore connection");
                
                // Get last connection parameters
                String host = prefs.getString(PREF_HOST, "");
                int port = prefs.getInt(PREF_PORT, 0);
                String callsign = prefs.getString(PREF_CALLSIGN, "");
                int talkgroup = prefs.getInt(PREF_TALKGROUP, 0);
                
                if (!host.isEmpty() && port > 0) {
                    // Start the VoIP service to restore connection
                    VoipBackgroundService.startVoipService(context, host, port, callsign, talkgroup);
                    Log.d(TAG, "VoIP service restart requested after boot");
                }
                
                // Clear the flag to prevent unnecessary restarts
                prefs.edit().putBoolean(PREF_WAS_CONNECTED, false).apply();
            }
        }
    }
    
    // Static methods for saving/clearing connection state
    public static void saveConnectionState(Context context, String host, int port, String callsign, int talkgroup) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit()
            .putBoolean(PREF_WAS_CONNECTED, true)
            .putString(PREF_HOST, host)
            .putInt(PREF_PORT, port)
            .putString(PREF_CALLSIGN, callsign)
            .putInt(PREF_TALKGROUP, talkgroup)
            .apply();
        Log.d(TAG, "Connection state saved for crash recovery");
    }
    
    public static void clearConnectionState(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putBoolean(PREF_WAS_CONNECTED, false).apply();
        Log.d(TAG, "Connection state cleared");
    }
}