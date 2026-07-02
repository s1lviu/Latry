package yo6say.latry;

import android.content.Context;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidJniRegistrationInstrumentationTest {
    private static final String HOST = "reflector-test.brainic.ro";
    private static final int PORT = 5337;
    private static final String CALLSIGN = "YO6SAY";
    private static final int TALKGROUP = 9;
    private static final long SERVICE_START_TIMEOUT_MS = 30000;

    @Before
    public void setUp() throws Exception {
        Assume.assumeTrue("Run this service-process JNI test via an explicit class selector",
                isExplicitSelection());
        AndroidIntegrationTestHooks.setEnabled(true);
        AndroidIntegrationTestHooks.reset();
        AndroidDeviceTestUtils.grantStandardRuntimePermissions();
        AndroidDeviceTestUtils.ensureServiceStopped();
    }

    @After
    public void tearDown() {
        AndroidIntegrationTestHooks.setEnabled(false);
    }

    @Test
    public void registeredNativeMethodsAreCallableWhenQtServiceIsRunning() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        String serviceComponent = context.getPackageName() + "/.VoipBackgroundService";

        VoipBackgroundService.startVoipService(context, HOST, PORT, CALLSIGN, TALKGROUP, false);

        AndroidDeviceTestUtils.waitUntil("Qt service start for JNI registration", SERVICE_START_TIMEOUT_MS,
                () -> AndroidDeviceTestUtils.isServiceCallStartCompleted(serviceComponent)
                        && VoipBackgroundService.getInstance() != null
                        && VoipBackgroundService.getInstance().isForegroundStartedForTesting());

        AndroidDeviceTestUtils.waitUntil("Qt JNI bridge after service launch", 15000, () -> {
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

        AndroidDeviceTestUtils.runOnMainSync(() -> {
            try {
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyServiceStarted",
                        new Class<?>[0]);
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyServiceStopped",
                        new Class<?>[0]);
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyCheckConnection",
                        new Class<?>[0]);
                AndroidDeviceTestUtils.invokePrivateStatic(
                        VoipBackgroundService.class,
                        "notifyAndroidControlEvent",
                        new Class<?>[] {int.class},
                        Integer.valueOf(LatryMediaSessionManager.EVENT_MEDIA_PLAY));
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryAudioRouteManager.class,
                        "notifyAudioRoutesChanged",
                        new Class<?>[] {String.class, String.class},
                        "speaker",
                        "[\"speaker\"]");
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryActivity.class,
                        "notifyActivityPaused",
                        new Class<?>[0]);
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryActivity.class,
                        "notifyActivityResumed",
                        new Class<?>[0]);
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryTranscriptionManager.class,
                        "notifyPartialTranscription",
                        new Class<?>[] {String.class},
                        "partial");
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryTranscriptionManager.class,
                        "notifyFinalTranscription",
                        new Class<?>[] {String.class},
                        "final");
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryTranscriptionManager.class,
                        "notifyTranscriptionError",
                        new Class<?>[] {int.class, String.class},
                        Integer.valueOf(5),
                        "error");
                AndroidDeviceTestUtils.invokePrivateStatic(
                        LatryTranscriptionManager.class,
                        "notifyTranscriptionStopped",
                        new Class<?>[0]);
            } catch (Exception exception) {
                throw new AssertionError("JNI bridge reflection failed", exception);
            }
        });
    }

    private static boolean isExplicitSelection() {
        String selector = InstrumentationRegistry.getArguments().getString("class", "");
        return selector.contains(AndroidJniRegistrationInstrumentationTest.class.getSimpleName());
    }
}
