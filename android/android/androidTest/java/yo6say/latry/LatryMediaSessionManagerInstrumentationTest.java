package yo6say.latry;

import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.PlaybackState;
import android.util.Log;
import android.view.KeyEvent;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class LatryMediaSessionManagerInstrumentationTest {
    private static final String TAG = "LatryDeviceTest";
    private LatryMediaSessionManager manager;
    private EventRecorder eventRecorder;

    @Before
    public void setUp() throws Exception {
        Context context = AndroidDeviceTestUtils.targetContext();
        eventRecorder = new EventRecorder();
        LatryAudioTrackPlayer.stopPlayback();
        LatryAudioRecordInput.stopCapture();
        LatryAudioRecordInput.releaseCapture();
        LatryAudioRouteManager.stopMonitoring();
        AndroidDeviceTestUtils.ensureServiceStopped();

        AndroidDeviceTestUtils.runOnMainSync(() -> {
            manager = new LatryMediaSessionManager(context, eventRecorder);
            manager.initialize();
            manager.updateConnectionStatus(true, "YO6SAY", 9);
        });
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
    }

    @After
    public void tearDown() {
        if (manager == null) {
            return;
        }

        AndroidDeviceTestUtils.runOnMainSync(() -> {
            manager.release();
            manager = null;
        });
        LatryAudioTrackPlayer.stopPlayback();
        LatryAudioRecordInput.stopCapture();
        LatryAudioRecordInput.releaseCapture();
        LatryAudioRouteManager.stopMonitoring();
    }

    @Test
    public void transportControlsAndMediaButtonsDispatchCallbacks() throws Exception {
        MediaController controller = manager.getControllerForTesting();
        assertNotNull(controller);

        controller.getTransportControls().play();
        AndroidDeviceTestUtils.waitUntil("media play callback", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_MEDIA_PLAY);

        controller.getTransportControls().pause();
        AndroidDeviceTestUtils.waitUntil("media pause callback", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_MEDIA_PAUSE);

        controller.getTransportControls().stop();
        AndroidDeviceTestUtils.waitUntil("media stop callback", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_MEDIA_STOP);

        PlaybackState initialState = controller.getPlaybackState();
        assertNotNull(initialState);
        assertEquals(1, initialState.getCustomActions().size());

        controller.getTransportControls().sendCustomAction(
                initialState.getCustomActions().get(0).getAction(), null);
        AndroidDeviceTestUtils.waitUntil("PTT press custom action", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_PTT_PRESS);

        AndroidDeviceTestUtils.runOnMainSync(() -> manager.updateTXStatus(true));
        PlaybackState txState = controller.getPlaybackState();
        assertNotNull(txState);
        assertEquals(1, txState.getCustomActions().size());

        controller.getTransportControls().sendCustomAction(
                txState.getCustomActions().get(0).getAction(), null);
        AndroidDeviceTestUtils.waitUntil("PTT release custom action", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_PTT_RELEASE);

        assertTrue(controller.dispatchMediaButtonEvent(
                new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_HEADSETHOOK)));
        AndroidDeviceTestUtils.waitUntil("headset down media button", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_PTT_PRESS);

        assertTrue(controller.dispatchMediaButtonEvent(
                new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_HEADSETHOOK)));
        AndroidDeviceTestUtils.waitUntil("headset up media button", 5000,
                () -> eventRecorder.getLastEvent() == LatryMediaSessionManager.EVENT_PTT_RELEASE);
    }

    @Test
    public void metadataReflectsConnectionAndReceiveState() throws Exception {
        MediaController controller = manager.getControllerForTesting();
        assertNotNull(controller);

        Log.i(TAG, "Updating media session RX state");
        AndroidDeviceTestUtils.runOnMainSync(() -> manager.updateRXStatus(true, "YO6ABC"));
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();

        AndroidDeviceTestUtils.waitUntil("media session metadata update", 10000, () -> {
            MediaMetadata metadata = controller.getMetadata();
            return metadata != null && "RX: YO6ABC".equals(
                    metadata.getString(MediaMetadata.METADATA_KEY_ARTIST));
        });

        MediaMetadata metadata = controller.getMetadata();
        assertNotNull(metadata);
        assertEquals("TG 9", metadata.getString(MediaMetadata.METADATA_KEY_TITLE));
        assertEquals("RX: YO6ABC", metadata.getString(MediaMetadata.METADATA_KEY_ARTIST));
    }

    private static final class EventRecorder implements LatryMediaSessionManager.ControlEventListener {
        private final List<Integer> events = new ArrayList<>();

        @Override
        public synchronized void onControlEvent(int eventType) {
            events.add(Integer.valueOf(eventType));
        }

        synchronized int getLastEvent() {
            if (events.isEmpty()) {
                return 0;
            }
            return events.get(events.size() - 1).intValue();
        }
    }
}
