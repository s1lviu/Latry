package yo6say.latry;

import android.content.Context;

public final class VoipBackgroundService {
    static final String ACTION_START_VOIP = "START_VOIP";
    static final String EXTRA_MONITOR_CONNECTION = "monitor_connection";
    public static final String ACTION_PTT_PRESSED = "yo6say.latry.action.PTT_PRESSED";
    public static final String ACTION_PTT_RELEASED = "yo6say.latry.action.PTT_RELEASED";

    public static int startCount = 0;
    public static int stopCount = 0;
    public static int dispatchCount = 0;
    public static Context lastContext = null;
    public static String lastHost = "";
    public static int lastPort = 0;
    public static String lastCallsign = "";
    public static int lastTalkgroup = 0;
    public static boolean lastMonitorConnection = false;
    public static boolean lastPttPressed = false;
    public static boolean serviceRunning = false;
    public static boolean connected = false;

    private VoipBackgroundService() {
    }

    public static void resetTestState() {
        startCount = 0;
        stopCount = 0;
        dispatchCount = 0;
        lastContext = null;
        lastHost = "";
        lastPort = 0;
        lastCallsign = "";
        lastTalkgroup = 0;
        lastMonitorConnection = false;
        lastPttPressed = false;
        serviceRunning = false;
        connected = false;
    }

    public static void startVoipService(Context context, String host, int port, String callsign,
                                        int talkgroup, boolean monitorConnection) {
        startCount += 1;
        lastContext = context;
        lastHost = host;
        lastPort = port;
        lastCallsign = callsign;
        lastTalkgroup = talkgroup;
        lastMonitorConnection = monitorConnection;
        serviceRunning = true;
    }

    public static void dispatchPTTAction(Context context, boolean pressed) {
        dispatchCount += 1;
        lastContext = context;
        lastPttPressed = pressed;
    }

    public static boolean isRunning() {
        return serviceRunning;
    }

    public static boolean shouldClaimOrderedPttBroadcasts() {
        return connected;
    }

    public static void stopVoipService(Context context) {
        stopCount += 1;
        lastContext = context;
        serviceRunning = false;
        connected = false;
    }
}
