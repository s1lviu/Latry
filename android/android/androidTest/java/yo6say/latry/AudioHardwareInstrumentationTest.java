package yo6say.latry;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.MediaRecorder;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class AudioHardwareInstrumentationTest {
    private static final String TAG = "LatryDeviceTest";

    @Before
    public void setUp() throws Exception {
        AndroidIntegrationTestHooks.setEnabled(true);
        AndroidIntegrationTestHooks.reset();
        AndroidDeviceTestUtils.grantStandardRuntimePermissions();
        LatryAudioTrackPlayer.stopPlayback();
        LatryAudioRecordInput.releaseCapture();
        LatryAudioRouteManager.stopMonitoring();
    }

    @After
    public void tearDown() throws Exception {
        LatryAudioTrackPlayer.stopPlayback();
        LatryAudioRecordInput.stopCapture();
        LatryAudioRecordInput.releaseCapture();
        LatryAudioRouteManager.stopMonitoring();
        AndroidIntegrationTestHooks.setEnabled(false);
    }

    @Test
    public void routeMonitoringReportsAvailableRoutes() throws Exception {
        RouteSnapshot snapshot = resolveRouteSnapshot();
        AudioDeviceInfo playbackDevice = LatryAudioRouteManager.findPlaybackDevice(
                snapshot.context, snapshot.currentRoute);
        assertNotNull(playbackDevice);
    }

    @Test
    public void playbackStartsAndWritesToResolvedRoute() throws Exception {
        RouteSnapshot snapshot = resolveRouteSnapshot();
        Log.i(TAG, "Starting playback on route=" + snapshot.currentRoute);
        assertTrue(LatryAudioTrackPlayer.startPlayback(snapshot.context, snapshot.currentRoute));
        Log.i(TAG, "Playback started, verifying track state");
        assertTrue(LatryAudioTrackPlayer.hasActiveTrackForTesting());
        assertEquals(snapshot.currentRoute, LatryAudioTrackPlayer.getCurrentRouteForTesting());
        assertEquals(AudioAttributes.CONTENT_TYPE_SPEECH,
                LatryAudioTrackPlayer.getContentTypeForTesting());
        int encoding = LatryAudioTrackPlayer.getEncoding();
        assertTrue(encoding == AudioFormat.ENCODING_PCM_FLOAT
                || encoding == AudioFormat.ENCODING_PCM_16BIT);
        Log.i(TAG, "Writing PCM test frame using encoding=" + encoding);
        if (encoding == AudioFormat.ENCODING_PCM_FLOAT) {
            assertTrue(LatryAudioTrackPlayer.writePcmFloat(new float[160], 160) >= 0);
        } else {
            assertTrue(LatryAudioTrackPlayer.writePcm16(new short[160], 160) >= 0);
        }
        LatryAudioTrackPlayer.stopPlayback();
    }

    @Test
    public void capturePreparesAndStartsOnAvailableRoute() throws Exception {
        RouteSnapshot snapshot = resolveRouteSnapshot();
        String captureRoute = resolveCaptureRoute(snapshot.context, snapshot.currentRoute);
        Log.i(TAG, "Preparing capture on route=" + captureRoute);
        assertTrue(LatryAudioRecordInput.prepareCapture(snapshot.context, captureRoute));
        assertTrue(LatryAudioRecordInput.hasActiveRecordForTesting());
        assertEquals(captureRoute, LatryAudioRecordInput.getCurrentRouteForTesting());
        Log.i(TAG, "Starting capture");
        assertTrue(LatryAudioRecordInput.startCapture(snapshot.context, captureRoute));
        assertTrue(LatryAudioRecordInput.isRecordingForTesting());
        assertTrue(isSupportedCaptureSource(LatryAudioRecordInput.getCurrentAudioSourceForTesting()));
        assertTrue(LatryAudioRecordInput.getCurrentAudioSourceForTesting()
                != MediaRecorder.AudioSource.VOICE_COMMUNICATION);
        assertTrue(isSupportedCaptureSampleRate(LatryAudioRecordInput.getSampleRate()));
        assertTrue(LatryAudioRecordInput.getEncoding() == AudioFormat.ENCODING_PCM_FLOAT
                || LatryAudioRecordInput.getEncoding() == AudioFormat.ENCODING_PCM_16BIT);
        LatryAudioRecordInput.stopCapture();
        LatryAudioRecordInput.releaseCapture();
    }

    @Test
    public void bluetoothCaptureDoesNotFallbackToBuiltInMicWhenBluetoothInputIsAbsent() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        AudioDeviceInfo bluetoothCaptureDevice =
                LatryAudioRouteManager.findCaptureDevice(context, "bluetooth");
        if (bluetoothCaptureDevice != null) {
            Log.i(TAG, "Bluetooth capture device is present, type=" + bluetoothCaptureDevice.getType());
            assertTrue(LatryAudioRouteManager.prepareCaptureRoute(context, "bluetooth"));
            assertTrue(LatryAudioRecordInput.prepareCapture(context, "bluetooth"));
            assertTrue(isBluetoothCaptureDeviceType(
                    LatryAudioRecordInput.getPreferredDeviceTypeForTesting()));
            LatryAudioRecordInput.stopCapture();
            LatryAudioRecordInput.releaseCapture();
            return;
        }

        Log.i(TAG, "No Bluetooth capture input is present; verifying no built-in mic fallback");
        assertFalse(LatryAudioRouteManager.prepareCaptureRoute(context, "bluetooth"));
        assertFalse(LatryAudioRecordInput.prepareCapture(context, "bluetooth"));
        assertFalse(LatryAudioRecordInput.hasActiveRecordForTesting());
    }

    @Test
    public void routeSwitchingReappliesPreferredRouteWhenMultipleRoutesPresent() throws Exception {
        RouteSnapshot snapshot = resolveRouteSnapshot();
        if (snapshot.routes.size() < 2) {
            Log.i(TAG, "Skipping route switching assertions, only one route available");
            return;
        }

        for (String route : snapshot.routes) {
            Log.i(TAG, "Switching preferred route to " + route);
            LatryAudioRouteManager.setPreferredRoute(snapshot.context, route);
            LatryAudioRouteManager.reapplyPreferredRoute(snapshot.context);
            final String expectedRoute = route;
            AndroidDeviceTestUtils.waitUntil("route switch to " + route, 5000,
                    () -> expectedRoute.equals(LatryAudioRouteManager.getCurrentRoute()));
            assertEquals(route, LatryAudioRouteManager.getCurrentRoute());
        }
    }

    private static RouteSnapshot resolveRouteSnapshot() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        Log.i(TAG, "Starting route monitoring");
        LatryAudioRouteManager.startMonitoring(context);
        AndroidDeviceTestUtils.waitUntil("audio route monitoring state", 5000,
                () -> LatryAudioRouteManager.isMonitoringForTesting()
                        && !LatryAudioRouteManager.getAvailableRoutesJson().isEmpty()
                        && !AndroidIntegrationTestHooks.getLastAvailableRoutesJson().isEmpty());

        List<String> routes = AndroidDeviceTestUtils.parseRoutesJson(
                LatryAudioRouteManager.getAvailableRoutesJson());
        Log.i(TAG, "Resolved routes=" + routes);
        assertFalse(routes.isEmpty());
        assertTrue(routes.contains("speaker"));

        String currentRoute = LatryAudioRouteManager.getCurrentRoute();
        Log.i(TAG, "Current route=" + currentRoute);
        assertNotNull(currentRoute);
        assertFalse(currentRoute.isEmpty());
        assertEquals(currentRoute, AndroidIntegrationTestHooks.getLastCurrentRoute());

        return new RouteSnapshot(context, routes, currentRoute);
    }

    private static String resolveCaptureRoute(Context context, String currentRoute) {
        String captureRoute = currentRoute;
        AudioDeviceInfo captureDevice = LatryAudioRouteManager.findCaptureDevice(context, captureRoute);
        if (captureDevice == null && !"speaker".equals(captureRoute)) {
            captureRoute = "speaker";
        }
        return captureRoute;
    }

    private static boolean isSupportedCaptureSampleRate(int sampleRate) {
        return sampleRate == 48000
                || sampleRate == 44100
                || sampleRate == 32000
                || sampleRate == 24000
                || sampleRate == 16000;
    }

    private static boolean isSupportedCaptureSource(int audioSource) {
        return audioSource == MediaRecorder.AudioSource.MIC
                || audioSource == MediaRecorder.AudioSource.DEFAULT
                || audioSource == MediaRecorder.AudioSource.VOICE_RECOGNITION;
    }

    private static boolean isBluetoothCaptureDeviceType(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || deviceType == 26;
    }

    private static final class RouteSnapshot {
        final Context context;
        final List<String> routes;
        final String currentRoute;

        RouteSnapshot(Context context, List<String> routes, String currentRoute) {
            this.context = context;
            this.routes = routes;
            this.currentRoute = currentRoute;
        }
    }
}
