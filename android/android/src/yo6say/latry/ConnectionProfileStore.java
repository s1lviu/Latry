package yo6say.latry;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

/**
 * Persists and retrieves the last connection profile.
 * Used by VoipBackgroundService and Android Auto reconnect.
 */
public final class ConnectionProfileStore {
    private static final String TAG = "ConnectionProfileStore";
    private static final String PREFS_NAME = "LatryVoipPrefs";
    private static final String PREF_WAS_CONNECTED = "was_connected";
    private static final String PREF_HOST = "last_host";
    private static final String PREF_PORT = "last_port";
    private static final String PREF_CALLSIGN = "last_callsign";
    private static final String PREF_TALKGROUP = "last_talkgroup";
    private static final String PREF_AUTH_KEY = "last_auth_key";
    private static final String PREF_MONITORED_TALKGROUPS = "last_monitored_talkgroups";
    private static final String PREF_TG_SELECT_TIMEOUT = "last_tg_select_timeout";
    private static final int DEFAULT_TG_SELECT_TIMEOUT_SECONDS = 30;

    private ConnectionProfileStore() {}

    public static void saveConnectionState(Context context, String host, int port, String callsign,
                                           int talkgroup, String authKey, String monitoredTalkgroups,
                                           int tgSelectTimeout) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit()
            .putBoolean(PREF_WAS_CONNECTED, true)
            .putString(PREF_HOST, host)
            .putInt(PREF_PORT, port)
            .putString(PREF_CALLSIGN, callsign)
            .putInt(PREF_TALKGROUP, talkgroup)
            .putString(PREF_AUTH_KEY, authKey != null ? authKey : "")
            .putString(PREF_MONITORED_TALKGROUPS, monitoredTalkgroups != null ? monitoredTalkgroups : "")
            .putInt(PREF_TG_SELECT_TIMEOUT, tgSelectTimeout > 0 ? tgSelectTimeout : DEFAULT_TG_SELECT_TIMEOUT_SECONDS)
            .apply();
        Log.d(TAG, "Connection state saved for crash recovery and Android Auto reconnect");
    }

    public static void clearConnectionState(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        prefs.edit().putBoolean(PREF_WAS_CONNECTED, false).apply();
        Log.d(TAG, "Transient connection restore flag cleared (saved reconnect profile preserved)");
    }

    public static boolean wasConnectedBeforeRestart(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getBoolean(PREF_WAS_CONNECTED, false);
    }

    public static boolean hasSavedConnectionProfile(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return !prefs.getString(PREF_HOST, "").isEmpty()
                && prefs.getInt(PREF_PORT, 0) > 0
                && !prefs.getString(PREF_CALLSIGN, "").isEmpty()
                && !prefs.getString(PREF_AUTH_KEY, "").isEmpty();
    }

    public static String getSavedHost(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getString(PREF_HOST, "");
    }

    public static int getSavedPort(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getInt(PREF_PORT, 0);
    }

    public static String getSavedCallsign(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getString(PREF_CALLSIGN, "");
    }

    public static int getSavedTalkgroup(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getInt(PREF_TALKGROUP, 0);
    }

    public static String getSavedAuthKey(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getString(PREF_AUTH_KEY, "");
    }

    public static String getSavedMonitoredTalkgroups(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        return prefs.getString(PREF_MONITORED_TALKGROUPS, "");
    }

    public static int getSavedTgSelectTimeout(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        int timeout = prefs.getInt(PREF_TG_SELECT_TIMEOUT, DEFAULT_TG_SELECT_TIMEOUT_SECONDS);
        return timeout > 0 ? timeout : DEFAULT_TG_SELECT_TIMEOUT_SECONDS;
    }
}
