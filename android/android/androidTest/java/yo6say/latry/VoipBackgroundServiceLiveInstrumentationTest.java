package yo6say.latry;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiManager;
import android.os.SystemClock;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class VoipBackgroundServiceLiveInstrumentationTest {
    private static final String DEFAULT_HOST = "svx.145500.xyz";
    private static final int DEFAULT_PORT = 5337;
    private static final String DEFAULT_CALLSIGN = "A1";
    private static final String DEFAULT_AUTH_KEY = "test";
    private static final int DEFAULT_TALKGROUP = 1;
    private static final int TG_SELECT_TIMEOUT_SECONDS = 30;
    private static final long SERVICE_START_TIMEOUT_MS = 30000;
    private static final long CONNECT_TIMEOUT_MS = 60000;
    private static final long NETWORK_HANDOVER_TIMEOUT_MS = 60000;
    private static final long WIFI_RESTORE_TIMEOUT_MS = 180000;
    private static final long PARROT_RX_TIMEOUT_MS = 45000;

    private boolean initialWifiEnabled = true;

    @Before
    public void setUp() throws Exception {
        Assume.assumeTrue("Run this live service test via an explicit class selector",
                isExplicitSelection());
        AndroidIntegrationTestHooks.setEnabled(true);
        AndroidIntegrationTestHooks.reset();
        AndroidDeviceTestUtils.grantStandardRuntimePermissions();
        initialWifiEnabled = isWifiEnabled();
        if (!initialWifiEnabled) {
            setWifiEnabled(true);
        }
        waitForDefaultTransport(AndroidDeviceTestUtils.targetContext(),
                NetworkHandoverMonitor.TRANSPORT_WIFI,
                30000,
                "validated default Wi-Fi before live reflector test");
        AndroidDeviceTestUtils.ensureServiceStopped();
        ConnectionProfileStore.clearConnectionState(AndroidDeviceTestUtils.targetContext());
    }

    @After
    public void tearDown() {
        Context context = AndroidDeviceTestUtils.targetContext();
        try {
            if (VoipBackgroundService.getInstance() != null) {
                AndroidDeviceTestUtils.runOnMainSync(() -> {
                    try {
                        AndroidDeviceTestUtils.invokePrivateStatic(
                                VoipBackgroundService.class,
                                "notifyAndroidControlEvent",
                                new Class<?>[] {int.class},
                                Integer.valueOf(LatryMediaSessionManager.EVENT_MEDIA_STOP));
                    } catch (Exception ignored) {
                    }
                });
                AndroidDeviceTestUtils.waitUntil("live service disconnect", 15000, () -> {
                    VoipBackgroundService service = VoipBackgroundService.getInstance();
                    return service == null || !service.isConnectedForTesting();
                });
            }
        } catch (Throwable ignored) {
        }
        try {
            if (isWifiEnabled() != initialWifiEnabled) {
                setWifiEnabled(initialWifiEnabled);
            }
            waitForDefaultTransport(context,
                    initialWifiEnabled
                            ? NetworkHandoverMonitor.TRANSPORT_WIFI
                            : NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                    30000,
                    "restored default transport after live service test");
        } catch (Throwable ignored) {
        }
        ConnectionProfileStore.clearConnectionState(context);
        AndroidIntegrationTestHooks.setEnabled(false);
    }

    @Test
    public void mediaPlayReconnectsToLiveReflectorAndSchedulesKeepAlive() throws Exception {
        Assume.assumeTrue("Run this live reflector test via an explicit method selector",
                isExplicitSelectionFor("mediaPlayReconnectsToLiveReflectorAndSchedulesKeepAlive"));
        Context context = AndroidDeviceTestUtils.targetContext();

        startLiveReflectorService(context);
        connectLiveReflector();
        waitForLiveReflectorConnection();

        VoipBackgroundService service = VoipBackgroundService.getInstance();
        assertTrue(service != null && service.isConnectedForTesting());
        assertEquals(liveTalkgroup(), service.getTalkGroupForTesting());
        assertTrue(service.getConnectionStatusForTesting().contains("Connected"));
        assertTrue(service.isConnectionMonitoringEnabledForTesting());

        AndroidDeviceTestUtils.waitUntil("live keep-alive callback", 25000,
                () -> AndroidIntegrationTestHooks.getCheckConnectionCount() > 0);

        disconnectLiveReflector();
    }

    @Test
    public void wifiToCellularAndBackReconnectsLiveReflector() throws Exception {
        Assume.assumeTrue("Run this live handover test via an explicit method selector",
                isExplicitSelectionFor("wifiToCellularAndBackReconnectsLiveReflector"));
        Context context = AndroidDeviceTestUtils.targetContext();

        startLiveReflectorService(context);
        connectLiveReflector();
        waitForLiveReflectorConnection();

        int networkGeneration = getServiceNetworkGeneration();
        int networkTransport = getServiceNetworkTransport();
        int connectionGeneration = getConnectionStatusUpdateGeneration();
        AndroidIntegrationTestHooks.setEnabled(false);
        setWifiEnabled(false);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                NETWORK_HANDOVER_TIMEOUT_MS,
                "default cellular handover");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                networkGeneration,
                networkTransport,
                "service-observed cellular handover");
        waitForLiveReflectorConnectionAfter(connectionGeneration);

        networkGeneration = getServiceNetworkGeneration();
        networkTransport = getServiceNetworkTransport();
        connectionGeneration = getConnectionStatusUpdateGeneration();
        setWifiEnabled(true);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_WIFI,
                WIFI_RESTORE_TIMEOUT_MS,
                "default Wi-Fi restoration");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_WIFI,
                networkGeneration,
                networkTransport,
                "service-observed Wi-Fi restoration");
        waitForLiveReflectorConnectionAfter(connectionGeneration);

        VoipBackgroundService service = VoipBackgroundService.getInstance();
        assertTrue(service != null && service.isConnectedForTesting());
        assertEquals(liveTalkgroup(), service.getTalkGroupForTesting());
        assertTrue(service.getConnectionStatusForTesting().contains("Connected"));

        disconnectLiveReflector();
    }

    @Test
    public void wifiToCellularDuringTransmitReconnectsAndParrotResponds() throws Exception {
        Assume.assumeTrue("Run this live TX handover test via an explicit method selector",
                isExplicitSelectionFor("wifiToCellularDuringTransmitReconnectsAndParrotResponds"));
        Context context = AndroidDeviceTestUtils.targetContext();

        startLiveReflectorService(context);
        connectLiveReflector();
        waitForLiveReflectorConnection();

        pressPtt();
        waitForTransmitting(true, "live TX before Wi-Fi handover");
        SystemClock.sleep(1500);

        int networkGeneration = getServiceNetworkGeneration();
        int networkTransport = getServiceNetworkTransport();
        int connectionGeneration = getConnectionStatusUpdateGeneration();
        AndroidIntegrationTestHooks.setEnabled(false);
        setWifiEnabled(false);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                NETWORK_HANDOVER_TIMEOUT_MS,
                "default cellular handover during TX");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                networkGeneration,
                networkTransport,
                "service-observed cellular handover during TX");
        waitForLiveReflectorConnectionAfter(connectionGeneration);
        waitForTransmitting(true, "live TX resumes after cellular handover",
                CONNECT_TIMEOUT_MS,
                true);

        SystemClock.sleep(2500);
        int receiveGeneration = getReceiveEventGeneration();
        releasePtt();
        waitForTransmitting(false, "live TX released after cellular handover", 15000, false);
        waitForParrotReceiveEventAfter(receiveGeneration);

        networkGeneration = getServiceNetworkGeneration();
        networkTransport = getServiceNetworkTransport();
        connectionGeneration = getConnectionStatusUpdateGeneration();
        setWifiEnabled(true);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_WIFI,
                WIFI_RESTORE_TIMEOUT_MS,
                "default Wi-Fi restoration after TX handover");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_WIFI,
                networkGeneration,
                networkTransport,
                "service-observed Wi-Fi restoration after TX handover");
        waitForLiveReflectorConnectionAfter(connectionGeneration);

        disconnectLiveReflector();
    }

    @Test
    public void wifiToCellularDuringParrotReceiveReconnectsLiveReflector() throws Exception {
        Assume.assumeTrue("Run this live RX handover test via an explicit method selector",
                isExplicitSelectionFor("wifiToCellularDuringParrotReceiveReconnectsLiveReflector"));
        Context context = AndroidDeviceTestUtils.targetContext();

        startLiveReflectorService(context);
        connectLiveReflector();
        waitForLiveReflectorConnection();

        transmitToParrot();
        waitForParrotReceive();

        VoipBackgroundService receivingService = VoipBackgroundService.getInstance();
        assertTrue(receivingService != null && receivingService.isReceivingForTesting());

        int networkGeneration = getServiceNetworkGeneration();
        int networkTransport = getServiceNetworkTransport();
        int connectionGeneration = getConnectionStatusUpdateGeneration();
        AndroidIntegrationTestHooks.setEnabled(false);
        setWifiEnabled(false);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                NETWORK_HANDOVER_TIMEOUT_MS,
                "default cellular handover during parrot RX");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_CELLULAR,
                networkGeneration,
                networkTransport,
                "service-observed cellular handover during parrot RX");
        waitForReceiving(false, "live RX cleared by cellular handover", 15000, false);
        waitForLiveReflectorConnectionAfter(connectionGeneration);

        networkGeneration = getServiceNetworkGeneration();
        networkTransport = getServiceNetworkTransport();
        connectionGeneration = getConnectionStatusUpdateGeneration();
        setWifiEnabled(true);
        waitForDefaultTransport(context,
                NetworkHandoverMonitor.TRANSPORT_WIFI,
                WIFI_RESTORE_TIMEOUT_MS,
                "default Wi-Fi restoration after parrot RX handover");
        waitForServiceDefaultTransport(NetworkHandoverMonitor.TRANSPORT_WIFI,
                networkGeneration,
                networkTransport,
                "service-observed Wi-Fi restoration after parrot RX handover");
        waitForLiveReflectorConnectionAfter(connectionGeneration);

        disconnectLiveReflector();
    }

    private static boolean isExplicitSelection() {
        String selector = InstrumentationRegistry.getArguments().getString("class", "");
        return selector.contains(VoipBackgroundServiceLiveInstrumentationTest.class.getSimpleName());
    }

    private static boolean isExplicitSelectionFor(String methodName) {
        String selector = InstrumentationRegistry.getArguments().getString("class", "");
        return selector.contains("#" + methodName);
    }

    private static String liveHost() {
        return InstrumentationRegistry.getArguments().getString("latry.live.host", DEFAULT_HOST);
    }

    private static int livePort() {
        return liveIntArgument("latry.live.port", DEFAULT_PORT);
    }

    private static String liveCallsign() {
        return InstrumentationRegistry.getArguments().getString("latry.live.callsign", DEFAULT_CALLSIGN);
    }

    private static String liveAuthKey() {
        return InstrumentationRegistry.getArguments().getString("latry.live.auth_key", DEFAULT_AUTH_KEY);
    }

    private static int liveTalkgroup() {
        return liveIntArgument("latry.live.talkgroup", DEFAULT_TALKGROUP);
    }

    private static int liveIntArgument(String name, int defaultValue) {
        String value = InstrumentationRegistry.getArguments().getString(name);
        if (value == null || value.trim().isEmpty()) {
            return defaultValue;
        }

        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException exception) {
            throw new AssertionError("Invalid integer instrumentation argument for " + name + ": " + value,
                    exception);
        }
    }

    private static void startLiveReflectorService(Context context) throws Exception {
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";

        ConnectionProfileStore.saveConnectionState(context,
                liveHost(),
                livePort(),
                liveCallsign(),
                liveTalkgroup(),
                liveAuthKey(),
                "",
                TG_SELECT_TIMEOUT_SECONDS);

        VoipBackgroundService.startVoipService(context,
                liveHost(),
                livePort(),
                liveCallsign(),
                liveTalkgroup(),
                false);

        AndroidDeviceTestUtils.waitUntil("live Qt service start", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent)
                        && VoipBackgroundService.getInstance() != null
                        && VoipBackgroundService.getInstance().isForegroundStartedForTesting());

        AndroidDeviceTestUtils.waitUntil("live Qt JNI bridge", 15000, () -> {
            try {
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyServiceStarted",
                        new Class<?>[0]);
                return true;
            } catch (Throwable throwable) {
                return false;
            }
        });
    }

    private static void connectLiveReflector() throws Exception {
        dispatchControlEvent(LatryMediaSessionManager.EVENT_MEDIA_PLAY, "MEDIA_PLAY");
    }

    private static void disconnectLiveReflector() throws Exception {
        dispatchControlEvent(LatryMediaSessionManager.EVENT_MEDIA_STOP, "MEDIA_STOP");

        AndroidDeviceTestUtils.waitUntil("live reflector disconnect", 30000, () -> {
            VoipBackgroundService currentService = VoipBackgroundService.getInstance();
            return currentService != null
                    && !currentService.isConnectedForTesting()
                    && !currentService.isConnectionMonitoringEnabledForTesting();
        });
    }

    private static void waitForLiveReflectorConnection() throws Exception {
        waitForLiveReflectorConnectionAfter(-1);
    }

    private static void waitForLiveReflectorConnectionAfter(int minConnectionUpdateGeneration)
            throws Exception {
        AndroidDeviceTestUtils.waitUntil("live reflector connection", CONNECT_TIMEOUT_MS, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            return service != null
                    && service.getConnectionStatusUpdateGenerationForTesting()
                            > minConnectionUpdateGeneration
                    && service.isConnectedForTesting()
                    && service.getTalkGroupForTesting() == liveTalkgroup()
                    && service.isConnectionMonitoringEnabledForTesting();
        });
    }

    private static void transmitToParrot() throws Exception {
        pressPtt();
        waitForTransmitting(true, "live TX to TG1 parrot");
        SystemClock.sleep(2500);
        releasePtt();
        waitForTransmitting(false, "live TX released for TG1 parrot");
    }

    private static void pressPtt() throws Exception {
        dispatchControlEvent(LatryMediaSessionManager.EVENT_PTT_PRESS, "PTT_PRESS");
    }

    private static void releasePtt() throws Exception {
        dispatchControlEvent(LatryMediaSessionManager.EVENT_PTT_RELEASE, "PTT_RELEASE");
    }

    private static void dispatchControlEvent(int eventType, String description) throws Exception {
        AndroidDeviceTestUtils.runOnMainSync(() -> {
            try {
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyAndroidControlEvent",
                        new Class<?>[] {int.class},
                        Integer.valueOf(eventType));
            } catch (Exception exception) {
                throw new AssertionError("Failed to dispatch " + description + " to live reflector",
                        exception);
            }
        });
    }

    private static void waitForTransmitting(boolean transmitting, String description) throws Exception {
        waitForTransmitting(transmitting, description, 30000, true);
    }

    private static void waitForTransmitting(boolean transmitting,
                                            String description,
                                            long timeoutMs,
                                            boolean requireConnected) throws Exception {
        AndroidDeviceTestUtils.waitUntil(description, timeoutMs, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            return service != null
                    && (!requireConnected || service.isConnectedForTesting())
                    && service.isTransmittingForTesting() == transmitting;
        });
    }

    private static void waitForReceiving(boolean receiving,
                                         String description,
                                         long timeoutMs,
                                         boolean requireConnected) throws Exception {
        AndroidDeviceTestUtils.waitUntil(description, timeoutMs, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            return service != null
                    && (!requireConnected || service.isConnectedForTesting())
                    && service.isReceivingForTesting() == receiving;
        });
    }

    private static void waitForParrotReceive() throws Exception {
        AndroidDeviceTestUtils.waitUntil("TG1 parrot receive", PARROT_RX_TIMEOUT_MS, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            return service != null
                    && service.isConnectedForTesting()
                    && service.isReceivingForTesting()
                    && service.getCurrentTalkerForTesting() != null
                    && !service.getCurrentTalkerForTesting().isEmpty();
        });
    }

    private static void waitForParrotReceiveEventAfter(int minReceiveEventGeneration)
            throws Exception {
        AndroidDeviceTestUtils.waitUntil("TG1 parrot receive event", PARROT_RX_TIMEOUT_MS, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            return service != null
                    && service.isConnectedForTesting()
                    && service.getReceiveEventGenerationForTesting() > minReceiveEventGeneration
                    && service.getLastReceiveEventTalkerForTesting() != null
                    && !service.getLastReceiveEventTalkerForTesting().isEmpty();
        });
    }

    private static int getConnectionStatusUpdateGeneration() {
        VoipBackgroundService service = VoipBackgroundService.getInstance();
        return service != null ? service.getConnectionStatusUpdateGenerationForTesting() : -1;
    }

    private static int getReceiveEventGeneration() {
        VoipBackgroundService service = VoipBackgroundService.getInstance();
        return service != null ? service.getReceiveEventGenerationForTesting() : -1;
    }

    private static int getServiceNetworkGeneration() {
        VoipBackgroundService service = VoipBackgroundService.getInstance();
        return service != null ? service.getLastDefaultNetworkGenerationForTesting() : -1;
    }

    private static int getServiceNetworkTransport() {
        VoipBackgroundService service = VoipBackgroundService.getInstance();
        return service != null
                ? service.getLastDefaultNetworkTransportForTesting()
                : NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
    }

    private static boolean isWifiEnabled() {
        Context context = AndroidDeviceTestUtils.targetContext().getApplicationContext();
        WifiManager wifiManager = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
        return wifiManager != null && wifiManager.isWifiEnabled();
    }

    private static void setWifiEnabled(boolean enabled) throws Exception {
        AndroidDeviceTestUtils.runShellCommand("cmd wifi set-wifi-enabled "
                + (enabled ? "enabled" : "disabled"));
    }

    private static void waitForDefaultTransport(Context context,
                                                int expectedTransport,
                                                long timeoutMs,
                                                String description) throws Exception {
        AndroidDeviceTestUtils.waitUntil(description, timeoutMs, () ->
                getActiveTransport(context) == expectedTransport && hasValidatedDefaultNetwork(context));
    }

    private static void waitForServiceDefaultTransport(int expectedTransport,
                                                       int previousGeneration,
                                                       int previousTransport,
                                                       String description) throws Exception {
        AndroidDeviceTestUtils.waitUntil(description, NETWORK_HANDOVER_TIMEOUT_MS, () -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            int currentGeneration = service != null
                    ? service.getLastDefaultNetworkGenerationForTesting()
                    : -1;
            int currentTransport = service != null
                    ? service.getLastDefaultNetworkTransportForTesting()
                    : NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
            return service != null
                    && currentTransport == expectedTransport
                    && (currentGeneration > previousGeneration
                            || previousTransport != expectedTransport);
        });
    }

    private static int getActiveTransport(Context context) {
        ConnectivityManager connectivityManager =
                (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) {
            return NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
        }

        Network activeNetwork = connectivityManager.getActiveNetwork();
        if (activeNetwork == null) {
            return NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
        }

        NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(activeNetwork);
        if (capabilities == null) {
            return NetworkHandoverMonitor.TRANSPORT_UNKNOWN;
        }

        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            return NetworkHandoverMonitor.TRANSPORT_WIFI;
        }
        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
            return NetworkHandoverMonitor.TRANSPORT_CELLULAR;
        }
        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
            return NetworkHandoverMonitor.TRANSPORT_ETHERNET;
        }
        return NetworkHandoverMonitor.TRANSPORT_OTHER;
    }

    private static boolean hasValidatedDefaultNetwork(Context context) {
        ConnectivityManager connectivityManager =
                (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) {
            return false;
        }

        Network activeNetwork = connectivityManager.getActiveNetwork();
        if (activeNetwork == null) {
            return false;
        }

        NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(activeNetwork);
        return capabilities != null
                && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
    }
}
