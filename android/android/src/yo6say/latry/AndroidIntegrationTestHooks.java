package yo6say.latry;

import java.util.ArrayList;
import java.util.List;

public final class AndroidIntegrationTestHooks {
    private static boolean enabled = false;
    private static int serviceStartedCount = 0;
    private static int serviceStoppedCount = 0;
    private static int checkConnectionCount = 0;
    private static int networkStateChangedCount = 0;
    private static final List<Integer> controlEvents = new ArrayList<>();
    private static String lastCurrentRoute = "";
    private static String lastAvailableRoutesJson = "";
    private static int lastNetworkGeneration = 0;
    private static int lastNetworkReason = 0;
    private static boolean lastHasDefaultNetwork = false;
    private static boolean lastValidatedNetwork = false;
    private static int lastNetworkTransport = 0;
    private static boolean lastMeteredNetwork = false;
    private static boolean lastCaptivePortal = false;
    private static boolean lastRouteChanged = false;

    private AndroidIntegrationTestHooks() {
    }

    public static synchronized void setEnabled(boolean value) {
        enabled = value;
        resetLocked();
    }

    public static synchronized boolean isEnabled() {
        return enabled;
    }

    public static synchronized void reset() {
        resetLocked();
    }

    public static synchronized void recordServiceStarted() {
        if (!enabled) {
            return;
        }
        serviceStartedCount += 1;
    }

    public static synchronized void recordServiceStopped() {
        if (!enabled) {
            return;
        }
        serviceStoppedCount += 1;
    }

    public static synchronized void recordCheckConnection() {
        if (!enabled) {
            return;
        }
        checkConnectionCount += 1;
    }

    public static synchronized void recordControlEvent(int eventType) {
        if (!enabled) {
            return;
        }
        controlEvents.add(Integer.valueOf(eventType));
    }

    public static synchronized void recordAudioRoutesChanged(String currentRoute, String availableRoutesJson) {
        if (!enabled) {
            return;
        }
        lastCurrentRoute = currentRoute == null ? "" : currentRoute;
        lastAvailableRoutesJson = availableRoutesJson == null ? "" : availableRoutesJson;
    }

    public static synchronized void recordNetworkStateChanged(int generation,
                                                              int reason,
                                                              boolean hasDefaultNetwork,
                                                              boolean validated,
                                                              int transport,
                                                              boolean metered,
                                                              boolean captivePortal,
                                                              boolean routeChanged) {
        if (!enabled) {
            return;
        }
        networkStateChangedCount += 1;
        lastNetworkGeneration = generation;
        lastNetworkReason = reason;
        lastHasDefaultNetwork = hasDefaultNetwork;
        lastValidatedNetwork = validated;
        lastNetworkTransport = transport;
        lastMeteredNetwork = metered;
        lastCaptivePortal = captivePortal;
        lastRouteChanged = routeChanged;
    }

    public static synchronized int getServiceStartedCount() {
        return serviceStartedCount;
    }

    public static synchronized int getServiceStoppedCount() {
        return serviceStoppedCount;
    }

    public static synchronized int getCheckConnectionCount() {
        return checkConnectionCount;
    }

    public static synchronized int getLastControlEvent() {
        if (controlEvents.isEmpty()) {
            return 0;
        }
        return controlEvents.get(controlEvents.size() - 1).intValue();
    }

    public static synchronized int getControlEventCount() {
        return controlEvents.size();
    }

    public static synchronized int getNetworkStateChangedCount() {
        return networkStateChangedCount;
    }

    public static synchronized String getLastCurrentRoute() {
        return lastCurrentRoute;
    }

    public static synchronized String getLastAvailableRoutesJson() {
        return lastAvailableRoutesJson;
    }

    public static synchronized int getLastNetworkGeneration() {
        return lastNetworkGeneration;
    }

    public static synchronized int getLastNetworkReason() {
        return lastNetworkReason;
    }

    public static synchronized boolean getLastHasDefaultNetwork() {
        return lastHasDefaultNetwork;
    }

    public static synchronized boolean getLastValidatedNetwork() {
        return lastValidatedNetwork;
    }

    public static synchronized int getLastNetworkTransport() {
        return lastNetworkTransport;
    }

    public static synchronized boolean getLastMeteredNetwork() {
        return lastMeteredNetwork;
    }

    public static synchronized boolean getLastCaptivePortal() {
        return lastCaptivePortal;
    }

    public static synchronized boolean getLastRouteChanged() {
        return lastRouteChanged;
    }

    private static void resetLocked() {
        serviceStartedCount = 0;
        serviceStoppedCount = 0;
        checkConnectionCount = 0;
        networkStateChangedCount = 0;
        controlEvents.clear();
        lastCurrentRoute = "";
        lastAvailableRoutesJson = "";
        lastNetworkGeneration = 0;
        lastNetworkReason = 0;
        lastHasDefaultNetwork = false;
        lastValidatedNetwork = false;
        lastNetworkTransport = 0;
        lastMeteredNetwork = false;
        lastCaptivePortal = false;
        lastRouteChanged = false;
    }
}
