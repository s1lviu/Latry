package yo6say.latry;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.SystemClock;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry;
import androidx.test.runner.lifecycle.Stage;

import org.junit.After;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Collection;
import java.util.concurrent.atomic.AtomicReference;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

@RunWith(AndroidJUnit4.class)
public final class VoipBackgroundServiceInstrumentationTest {
    private static final int NOTIFICATION_ID = 1001;
    private static final String CHANNEL_ID = "voip_service_channel";
    private static final String HOST = "reflector-test.brainic.ro";
    private static final int PORT = 5337;
    private static final String CALLSIGN = "YO6SAY";
    private static final int TALKGROUP = 9;
    private static final long SERVICE_START_TIMEOUT_MS = 30000;

    @Before
    public void setUp() throws Exception {
        AndroidIntegrationTestHooks.setEnabled(true);
        AndroidIntegrationTestHooks.reset();
        AndroidDeviceTestUtils.grantStandardRuntimePermissions();
        AndroidDeviceTestUtils.ensureServiceStopped();
    }

    @After
    public void tearDown() throws Exception {
        if (shouldStopServiceInTearDown()) {
            AndroidDeviceTestUtils.ensureServiceStopped();
        }
        AndroidIntegrationTestHooks.setEnabled(false);
    }

    @Test
    public void foregroundServiceStartsAndProcessesExternalControls() throws Exception {
        Assume.assumeTrue("Run this dedicated-service smoke test via an explicit method selector",
                isExplicitSelectionFor("foregroundServiceStartsAndProcessesExternalControls"));
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);

        AndroidDeviceTestUtils.waitUntil("foreground service record", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServicePresent(serviceComponent));
        AndroidDeviceTestUtils.waitUntil("foreground service start completion", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        VoipBackgroundService.dispatchControlEvent(context, LatryMediaSessionManager.EVENT_MEDIA_PLAY);
        AndroidDeviceTestUtils.waitUntil("MEDIA_PLAY control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent() == LatryMediaSessionManager.EVENT_MEDIA_PLAY);

        VoipBackgroundService.dispatchPTTAction(context, true);
        AndroidDeviceTestUtils.waitUntil("PTT press control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent() == LatryMediaSessionManager.EVENT_PTT_PRESS);

        VoipBackgroundService.dispatchPTTAction(context, false);
        AndroidDeviceTestUtils.waitUntil("PTT release control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent() == LatryMediaSessionManager.EVENT_PTT_RELEASE);
    }

    @Test
    public void meigVendorBroadcastsDispatchPttControlEvents() throws Exception {
        Assume.assumeTrue("Run this Meig vendor broadcast test via an explicit method selector",
                isExplicitSelectionFor("meigVendorBroadcastsDispatchPttControlEvents"));
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";
        Intent launchIntent = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());

        if (launchIntent == null) {
            throw new AssertionError("Launch intent is missing for " + context.getPackageName());
        }
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        context.startActivity(launchIntent);

        AndroidDeviceTestUtils.waitUntil("activity instance before Meig background PTT test", 15000,
                () -> LatryActivity.currentActivityInstanceForTesting() != null);

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);

        AndroidDeviceTestUtils.waitUntil("foreground service record for Meig PTT", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServicePresent(serviceComponent));
        AndroidDeviceTestUtils.waitUntil("foreground service start completion for Meig PTT",
                SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        LatryActivity activity = LatryActivity.currentActivityInstanceForTesting();
        assertNotNull(activity);

        AndroidDeviceTestUtils.runOnMainSync(activity::finish);

        AndroidDeviceTestUtils.waitUntil("LatryActivity background before Meig background PTT test", 15000,
                () -> !LatryActivity.isActivityVisible());
        AndroidDeviceTestUtils.waitUntil("service remains active after activity background for Meig PTT", 15000,
                () -> AndroidDeviceTestUtils.isServicePresent(serviceComponent));

        sendMeigBroadcast(context, "keycode", PTTButtonBroadcastReceiver.MEIG_PTT_KEYCODE,
                "action", 0);
        AndroidDeviceTestUtils.waitUntil("Meig PTT press control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent() == LatryMediaSessionManager.EVENT_PTT_PRESS);

        sendMeigBroadcast(context, "keyCode", PTTButtonBroadcastReceiver.MEIG_PTT_KEYCODE,
                "keyOperation", 1);
        AndroidDeviceTestUtils.waitUntil("Meig PTT release control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent() == LatryMediaSessionManager.EVENT_PTT_RELEASE);

        int controlEventCount = AndroidIntegrationTestHooks.getControlEventCount();
        sendMeigBroadcast(context, "keycode", 141, "action", 0);
        assertNoAdditionalControlEvents("non-PTT Meig key", controlEventCount, 1500);
    }

    @Test
    public void fixedHardwareKeyEventsDispatchPttControlEvents() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";
        Intent launchIntent = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());

        if (launchIntent == null) {
            throw new AssertionError("Launch intent is missing for " + context.getPackageName());
        }
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        context.startActivity(launchIntent);

        AndroidDeviceTestUtils.waitUntil("activity visibility before fixed hardware key test", 15000,
                LatryActivity::isActivityVisible);

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);
        AndroidDeviceTestUtils.waitUntil("service start before fixed hardware key test",
                SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        LatryActivity activity = getResumedActivity(LatryActivity.class);
        assertNotNull(activity);

        AndroidDeviceTestUtils.runOnMainSync(() -> assertTrue(activity.dispatchKeyEvent(
                new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN,
                        HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT))));
        AndroidDeviceTestUtils.waitUntil("fixed hardware key press control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent()
                        == LatryMediaSessionManager.EVENT_PTT_PRESS);

        AndroidDeviceTestUtils.runOnMainSync(() -> assertTrue(activity.dispatchKeyEvent(
                new android.view.KeyEvent(android.view.KeyEvent.ACTION_UP,
                        HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT))));
        AndroidDeviceTestUtils.waitUntil("fixed hardware key release control event", 15000,
                () -> AndroidIntegrationTestHooks.getLastControlEvent()
                        == LatryMediaSessionManager.EVENT_PTT_RELEASE);

        int controlEventCount = AndroidIntegrationTestHooks.getControlEventCount();
        AndroidDeviceTestUtils.runOnMainSync(() -> assertFalse(activity.dispatchKeyEvent(
                new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, 141))));
        assertNoAdditionalControlEvents("non-PTT fixed hardware key", controlEventCount, 1500);
    }

    @Test
    public void disconnectStateDropsForegroundWithoutStoppingService() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";
        Intent launchIntent = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());

        if (launchIntent == null) {
            throw new AssertionError("Launch intent is missing for " + context.getPackageName());
        }
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        context.startActivity(launchIntent);

        AndroidDeviceTestUtils.waitUntil("activity visibility before disconnect simulation", 15000,
                LatryActivity::isActivityVisible);

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);
        AndroidDeviceTestUtils.waitUntil("service start before disconnect simulation", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        AndroidDeviceTestUtils.waitUntil("service instance", SERVICE_START_TIMEOUT_MS,
                () -> VoipBackgroundService.getInstance() != null);
        assertFalse(VoipBackgroundService.getInstance().isQtServiceAttachedForTesting());

        AndroidDeviceTestUtils.runOnMainSync(() -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            service.updateConnectionStatus("Connected", true);
        });

        AndroidDeviceTestUtils.waitUntil("foreground promotion after simulated connect", 15000,
                () -> {
                    VoipBackgroundService service = VoipBackgroundService.getInstance();
                    return service != null && service.isForegroundStartedForTesting();
                });

        AndroidDeviceTestUtils.runOnMainSync(() -> {
            VoipBackgroundService service = VoipBackgroundService.getInstance();
            service.setConnectionMonitoringEnabled(false);
            service.updateConnectionStatus("Disconnected", false);
        });

        AndroidDeviceTestUtils.waitUntil("foreground removal after simulated disconnect", 15000,
                () -> {
                    VoipBackgroundService service = VoipBackgroundService.getInstance();
                    return service != null && !service.isForegroundStartedForTesting();
                });

        AndroidDeviceTestUtils.waitUntil("service remains active after disconnect", 15000,
                () -> AndroidDeviceTestUtils.isServicePresent(serviceComponent));
    }

    @Test
    public void connectionMonitoringStartsDefaultNetworkMonitorAndDispatchesSnapshots() throws Exception {
        Assume.assumeTrue("Run this dedicated-service network-monitor test via an explicit method selector",
                isExplicitSelectionFor("connectionMonitoringStartsDefaultNetworkMonitorAndDispatchesSnapshots"));
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);

        AndroidDeviceTestUtils.waitUntil("service start before network monitor assertions",
                SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));
        AndroidDeviceTestUtils.waitUntil("service instance for network monitor assertions",
                SERVICE_START_TIMEOUT_MS,
                () -> VoipBackgroundService.getInstance() != null);

        VoipBackgroundService service = VoipBackgroundService.getInstance();
        assertNotNull(service);
        assertTrue(service.isConnectionMonitoringEnabledForTesting());
        assertTrue(service.isNetworkMonitorActiveForTesting());

        AndroidDeviceTestUtils.runOnMainSync(() -> service.dispatchNetworkStateForTesting(
                7,
                NetworkHandoverMonitor.REASON_LINK_PROPERTIES_CHANGED,
                true,
                true,
                NetworkHandoverMonitor.TRANSPORT_WIFI,
                false,
                false,
                true));

        AndroidDeviceTestUtils.waitUntil("network snapshot dispatch", 15000,
                () -> AndroidIntegrationTestHooks.getNetworkStateChangedCount() >= 1);

        assertEquals(7, AndroidIntegrationTestHooks.getLastNetworkGeneration());
        assertEquals(NetworkHandoverMonitor.REASON_LINK_PROPERTIES_CHANGED,
                AndroidIntegrationTestHooks.getLastNetworkReason());
        assertTrue(AndroidIntegrationTestHooks.getLastHasDefaultNetwork());
        assertTrue(AndroidIntegrationTestHooks.getLastValidatedNetwork());
        assertEquals(NetworkHandoverMonitor.TRANSPORT_WIFI,
                AndroidIntegrationTestHooks.getLastNetworkTransport());
        assertFalse(AndroidIntegrationTestHooks.getLastMeteredNetwork());
        assertFalse(AndroidIntegrationTestHooks.getLastCaptivePortal());
        assertTrue(AndroidIntegrationTestHooks.getLastRouteChanged());
    }

    @Test
    public void finishingActivityDoesNotStopLiveBackgroundService() throws Exception {
        Assume.assumeTrue("Run this activity-finish smoke test via an explicit method selector",
                isExplicitSelectionFor("finishingActivityDoesNotStopLiveBackgroundService"));
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";
        Intent launchIntent = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());

        if (launchIntent == null) {
            throw new AssertionError("Launch intent is missing for " + context.getPackageName());
        }
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        context.startActivity(launchIntent);

        AndroidDeviceTestUtils.waitUntil("activity instance before background-service check", 15000,
                () -> LatryActivity.currentActivityInstanceForTesting() != null);

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);
        AndroidDeviceTestUtils.waitUntil("service start before activity finish", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        LatryActivity activity = LatryActivity.currentActivityInstanceForTesting();
        assertNotNull(activity);

        AndroidDeviceTestUtils.runOnMainSync(activity::finish);

        AndroidDeviceTestUtils.waitUntil("LatryActivity finish", 15000,
                () -> LatryActivity.currentActivityInstanceForTesting() == null);
        AndroidDeviceTestUtils.waitUntil("service remains active after activity finish", 15000,
                () -> AndroidDeviceTestUtils.isServicePresent(serviceComponent));
    }

    @Test
    public void serviceSkipsQtAttachWhenActivityLoaderOccupiesQtSingletonBeforeQtMarkedStarted()
            throws Exception {
        Assume.assumeTrue("Run this Qt loader conflict smoke test via an explicit method selector",
                isExplicitSelectionFor("serviceSkipsQtAttachWhenActivityLoaderOccupiesQtSingletonBeforeQtMarkedStarted"));
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";
        Intent intent = new Intent(context, QtLoaderConflictActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        context.startActivity(intent);

        AndroidDeviceTestUtils.waitUntil("loader-conflict activity resume", 15000,
                () -> getResumedActivity(QtLoaderConflictActivity.class) != null);
        try {
            QtLoaderConflictActivity activity = getResumedActivity(QtLoaderConflictActivity.class);
            assertNotNull(activity);

            occupyQtLoaderWithActivity(activity);

            AndroidDeviceTestUtils.waitUntil("Qt activity loader singleton", 15000, () -> {
                Object loaderInstance = getQtLoaderInstance();
                return loaderInstance != null
                        && "org.qtproject.qt.android.QtActivityLoader"
                        .equals(loaderInstance.getClass().getName());
            });
            assertFalse(getQtStartedForTesting());

            VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, true);

            AndroidDeviceTestUtils.waitUntil("service start under forced Qt loader conflict",
                    SERVICE_START_TIMEOUT_MS,
                    () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));
            AndroidDeviceTestUtils.waitUntil("service instance under forced Qt loader conflict",
                    SERVICE_START_TIMEOUT_MS,
                    () -> VoipBackgroundService.getInstance() != null);

            assertFalse(VoipBackgroundService.getInstance().isQtServiceAttachedForTesting());

            Object loaderInstance = getQtLoaderInstance();
            assertNotNull(loaderInstance);
            assertEquals("org.qtproject.qt.android.QtActivityLoader",
                    loaderInstance.getClass().getName());
        } finally {
            try {
                QtLoaderConflictActivity activity = getResumedActivity(QtLoaderConflictActivity.class);
                if (activity != null) {
                    AndroidDeviceTestUtils.runOnMainSync(activity::finish);
                    AndroidDeviceTestUtils.waitUntil("loader-conflict activity finish", 15000,
                            () -> getResumedActivity(QtLoaderConflictActivity.class) == null);
                }
            } finally {
                resetQtLoaderInstanceForTesting();
                assertNull(getQtLoaderInstance());
            }
        }
    }

    @Test
    public void serviceNotificationIsLowImportanceAndOnlyAlertsOnce() throws Exception {
        Assume.assumeTrue("Run this dedicated-service notification test via an explicit method selector",
                isExplicitSelectionFor("serviceNotificationIsLowImportanceAndOnlyAlertsOnce"));
        Context context = AndroidDeviceTestUtils.targetContext();

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, false);

        AndroidDeviceTestUtils.waitUntil("service instance for notification assertions", SERVICE_START_TIMEOUT_MS,
                () -> VoipBackgroundService.getInstance() != null
                        && VoipBackgroundService.getInstance().isForegroundStartedForTesting());

        NotificationManager manager =
                (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        assertNotNull(manager);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = manager.getNotificationChannel(CHANNEL_ID);
            assertNotNull(channel);
            assertEquals(NotificationManager.IMPORTANCE_LOW, channel.getImportance());
        }

        AndroidDeviceTestUtils.waitUntil("foreground notification visibility", 10000,
                () -> findActiveNotification(manager) != null);

        Notification notification = findActiveNotification(manager);
        assertNotNull(notification);
        assertTrue((notification.flags & Notification.FLAG_ONLY_ALERT_ONCE) != 0);
    }

    @Ignore("Foreground-service stop tears down the target process under instrumentation; covered by adb service smoke tests")
    @Test
    public void stopRequestStopsForegroundService() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, false);
        AndroidDeviceTestUtils.waitUntil("service start before stop", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent));

        VoipBackgroundService.stopVoipService(context);
        AndroidDeviceTestUtils.waitUntil("foreground service stop", SERVICE_START_TIMEOUT_MS,
                () -> !AndroidDeviceTestUtils.isServicePresent(serviceComponent));
    }

    private static Notification findActiveNotification(NotificationManager manager) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return null;
        }

        for (android.service.notification.StatusBarNotification statusBarNotification
                : manager.getActiveNotifications()) {
            if (statusBarNotification.getId() == NOTIFICATION_ID) {
                return statusBarNotification.getNotification();
            }
        }

        return null;
    }

    private static boolean isExplicitSelectionFor(String methodName) {
        String selector = InstrumentationRegistry.getArguments().getString("class", "");
        return selector.contains(VoipBackgroundServiceInstrumentationTest.class.getSimpleName()
                + "#" + methodName);
    }

    private static Object getQtLoaderInstance() throws Exception {
        return getPrivateStaticField("org.qtproject.qt.android.QtLoader", "m_instance");
    }

    private static boolean getQtStartedForTesting() throws Exception {
        Object stateDetails = invokePrivateStatic("org.qtproject.qt.android.QtNative", "getStateDetails");
        Field isStartedField = stateDetails.getClass().getDeclaredField("isStarted");
        isStartedField.setAccessible(true);
        return isStartedField.getBoolean(stateDetails);
    }

    private static void occupyQtLoaderWithActivity(Activity activity) throws Exception {
        invokePrivateStatic("org.qtproject.qt.android.QtActivityLoader", "getActivityLoader",
                new Class<?>[]{Activity.class}, activity);
    }

    private static void resetQtLoaderInstanceForTesting() throws Exception {
        if (getQtStartedForTesting()) {
            throw new AssertionError("Qt runtime unexpectedly started while resetting QtLoader");
        }
        setPrivateStaticField("org.qtproject.qt.android.QtLoader", "m_instance", null);
    }

    private static Object getPrivateStaticField(String className, String fieldName) throws Exception {
        Class<?> type = Class.forName(className);
        Field field = type.getDeclaredField(fieldName);
        field.setAccessible(true);
        return field.get(null);
    }

    private static void setPrivateStaticField(String className,
                                              String fieldName,
                                              Object value) throws Exception {
        Class<?> type = Class.forName(className);
        Field field = type.getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(null, value);
    }

    private static Object invokePrivateStatic(String className, String methodName) throws Exception {
        return invokePrivateStatic(className, methodName, new Class<?>[0]);
    }

    private static Object invokePrivateStatic(String className,
                                              String methodName,
                                              Class<?>[] parameterTypes,
                                              Object... args) throws Exception {
        Class<?> type = Class.forName(className);
        Method method = type.getDeclaredMethod(methodName, parameterTypes);
        method.setAccessible(true);
        try {
            return method.invoke(null, args);
        } catch (InvocationTargetException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof Exception) {
                throw (Exception) cause;
            }
            if (cause instanceof Error) {
                throw (Error) cause;
            }
            throw new AssertionError("Invocation failed for " + className + "::" + methodName, cause);
        }
    }

    private static <T extends Activity> T getResumedActivity(Class<T> activityClass) {
        AtomicReference<T> resumedActivity = new AtomicReference<>();
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            Collection<Activity> resumedActivities =
                    ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED);
            for (Activity activity : resumedActivities) {
                if (activityClass.isInstance(activity)) {
                    resumedActivity.set(activityClass.cast(activity));
                    break;
                }
            }
        });
        return resumedActivity.get();
    }

    private static void sendMeigBroadcast(Context context,
                                          String keyCodeExtraName,
                                          int keyCode,
                                          String actionExtraName,
                                          int action) {
        Intent intent = new Intent(PTTButtonBroadcastReceiver.ACTION_MEIG_KEY_EVENT);
        intent.setPackage(context.getPackageName());
        intent.putExtra(keyCodeExtraName, keyCode);
        intent.putExtra(actionExtraName, action);
        context.sendBroadcast(intent);
    }

    private static void assertNoAdditionalControlEvents(String description,
                                                        int initialCount,
                                                        long durationMs) {
        long deadline = SystemClock.uptimeMillis() + durationMs;
        while (SystemClock.uptimeMillis() < deadline) {
            if (AndroidIntegrationTestHooks.getControlEventCount() != initialCount) {
                fail("Unexpected control event while waiting for " + description);
            }
            SystemClock.sleep(100);
        }
    }

    private static boolean shouldStopServiceInTearDown() {
        return !isExplicitSelectionFor("foregroundServiceStartsAndProcessesExternalControls")
                && !isExplicitSelectionFor("connectionMonitoringStartsDefaultNetworkMonitorAndDispatchesSnapshots")
                && !isExplicitSelectionFor("serviceNotificationIsLowImportanceAndOnlyAlertsOnce");
    }
}
