package yo6say.latry;

import android.content.Intent;
import android.os.Build;
import android.view.KeyEvent;

public final class PTTButtonBroadcastReceiverSelfTest {
    private PTTButtonBroadcastReceiverSelfTest() {
    }

    public static void main(String[] args) {
        testDirectPttActionsDispatchExpectedState();
        testGenericPttActionUsesPressedAndStateExtras();
        testOrderedStandardPttBroadcastAutoClaimsWhenConnected();
        testOrderedMeigPttBroadcastAutoClaimsWhenConnected();
        testOrderedClaimRequiresConnectedLatryState();
        testInternalPttBroadcastsNeverAbort();
        testOrderedMediaButtonPttBroadcastClaimsOnlyWhenConnected();
        testMediaButtonHeadsetDispatchesPtt();
        testFixedHardwareMediaButtonDispatchesPtt();
        testMediaButtonFallbackPathWorksPreTiramisu();
        testUnsupportedActionsAreIgnored();
        testNullContextOrIntentAreIgnored();
        System.out.println("PTTButtonBroadcastReceiverSelfTest passed");
    }

    private static void testDirectPttActionsDispatchExpectedState() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();

        assertDispatch(receiver, context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN), true,
            "ACTION_PTT_DOWN should dispatch PTT press");
        assertDispatch(receiver, context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN_ALT), true,
            "ACTION_PTT_DOWN_ALT should dispatch PTT press");
        assertDispatch(receiver, context, new Intent(VoipBackgroundService.ACTION_PTT_PRESSED), true,
            "service press action should dispatch PTT press");
        assertDispatch(receiver, context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_UP), false,
            "ACTION_PTT_UP should dispatch PTT release");
        assertDispatch(receiver, context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_UP_ALT), false,
            "ACTION_PTT_UP_ALT should dispatch PTT release");
        assertDispatch(receiver, context, new Intent(VoipBackgroundService.ACTION_PTT_RELEASED), false,
            "service release action should dispatch PTT release");
    }

    private static void testGenericPttActionUsesPressedAndStateExtras() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();

        assertDispatch(receiver, context,
            new Intent(PTTButtonBroadcastReceiver.ACTION_PTT).putExtra("pressed", true),
            true,
            "pressed=true should dispatch press");
        assertDispatch(receiver, context,
            new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_KEY).putExtra("state", true),
            true,
            "state=true should dispatch press");
        assertDispatch(receiver, context,
            new Intent(PTTButtonBroadcastReceiver.ACTION_PTT).putExtra("pressed", false),
            false,
            "missing/false flags should dispatch release");
    }

    private static void testMediaButtonHeadsetDispatchesPtt() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();

        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_HEADSETHOOK),
            true,
            "headset ACTION_DOWN should dispatch press");
        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_HEADSETHOOK),
            false,
            "headset ACTION_UP should dispatch release");
        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_CALL),
            true,
            "call key ACTION_DOWN should dispatch press");
        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_CALL),
            false,
            "call key ACTION_UP should dispatch release");
    }

    private static void testOrderedStandardPttBroadcastAutoClaimsWhenConnected() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        VoipBackgroundService.connected = true;

        receiver.onReceive(context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN));

        assertEquals(1, VoipBackgroundService.dispatchCount,
                "ordered standard PTT broadcast should dispatch once");
        assertTrue(receiver.wasAbortBroadcastCalledForTesting(),
                "ordered standard PTT broadcast should be aborted automatically while connected");
    }

    private static void testOrderedMeigPttBroadcastAutoClaimsWhenConnected() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        VoipBackgroundService.connected = true;

        receiver.onReceive(context,
                new Intent(PTTButtonBroadcastReceiver.ACTION_MEIG_KEY_EVENT)
                        .putExtra("keycode", PTTButtonBroadcastReceiver.MEIG_PTT_KEYCODE)
                        .putExtra("action", 0));

        assertEquals(1, VoipBackgroundService.dispatchCount,
                "ordered Meig PTT broadcast should dispatch once");
        assertTrue(receiver.wasAbortBroadcastCalledForTesting(),
                "ordered Meig PTT broadcast should be aborted automatically while connected");
    }

    private static void testOrderedClaimRequiresConnectedLatryState() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        receiver.setOrderedBroadcastForTesting(true);

        VoipBackgroundService.resetTestState();
        receiver.onReceive(context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN));
        assertFalse(receiver.wasAbortBroadcastCalledForTesting(),
                "ordered broadcast should not abort while Latry is disconnected");

        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        VoipBackgroundService.serviceRunning = true;
        receiver.onReceive(context, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN));
        assertFalse(receiver.wasAbortBroadcastCalledForTesting(),
                "ordered broadcast should not abort just because the service is alive");
    }

    private static void testInternalPttBroadcastsNeverAbort() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        VoipBackgroundService.connected = true;

        receiver.onReceive(context, new Intent(VoipBackgroundService.ACTION_PTT_PRESSED));
        assertFalse(receiver.wasAbortBroadcastCalledForTesting(),
                "Latry internal PTT broadcasts must not be aborted");
    }

    private static void testOrderedMediaButtonPttBroadcastClaimsOnlyWhenConnected() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        HardwarePttSettingsStore.setPocButtonEnabled(context, true);

        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        receiver.onReceive(context,
                mediaButtonIntent(KeyEvent.ACTION_DOWN, HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS));
        assertFalse(receiver.wasAbortBroadcastCalledForTesting(),
                "fixed MEDIA_BUTTON PTT handling must not abort broadcasts while disconnected");

        receiver.setOrderedBroadcastForTesting(true);
        VoipBackgroundService.resetTestState();
        VoipBackgroundService.connected = true;
        receiver.onReceive(context,
                mediaButtonIntent(KeyEvent.ACTION_DOWN, HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS));
        assertTrue(receiver.wasAbortBroadcastCalledForTesting(),
                "fixed MEDIA_BUTTON PTT handling must abort ordered broadcasts while connected");
    }

    private static void testFixedHardwareMediaButtonDispatchesPtt() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();

        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_DOWN, HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT),
            true,
            "fixed vendor PTT ACTION_DOWN should dispatch press");
        assertDispatch(receiver, context,
            mediaButtonIntent(KeyEvent.ACTION_UP, HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT),
            false,
            "fixed vendor PTT ACTION_UP should dispatch release");
    }

    private static void testMediaButtonFallbackPathWorksPreTiramisu() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        int originalSdkInt = Build.VERSION.SDK_INT;
        Build.VERSION.SDK_INT = Build.VERSION_CODES.TIRAMISU - 1;
        try {
            assertDispatch(receiver, context,
                mediaButtonIntent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_HEADSETHOOK),
                true,
                "pre-Tiramisu fallback should still extract the KeyEvent");
        } finally {
            Build.VERSION.SDK_INT = originalSdkInt;
        }
    }

    private static void testUnsupportedActionsAreIgnored() {
        FakeContext context = new FakeContext();
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        VoipBackgroundService.resetTestState();

        receiver.onReceive(context, new Intent("yo6say.latry.action.UNKNOWN"));
        receiver.onReceive(context, mediaButtonIntent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PLAY));
        receiver.onReceive(context, new Intent(Intent.ACTION_MEDIA_BUTTON));

        assertEquals(0, VoipBackgroundService.dispatchCount, "unsupported actions should not dispatch PTT");
    }

    private static void testNullContextOrIntentAreIgnored() {
        PTTButtonBroadcastReceiver receiver = new PTTButtonBroadcastReceiver();
        FakeContext context = new FakeContext();
        VoipBackgroundService.resetTestState();

        receiver.onReceive(null, new Intent(PTTButtonBroadcastReceiver.ACTION_PTT_DOWN));
        receiver.onReceive(context, null);

        assertEquals(0, VoipBackgroundService.dispatchCount, "null receiver arguments should be ignored");
    }

    private static Intent mediaButtonIntent(int action, int keyCode) {
        return new Intent(Intent.ACTION_MEDIA_BUTTON)
            .putExtra(Intent.EXTRA_KEY_EVENT, new KeyEvent(action, keyCode));
    }

    private static void assertDispatch(PTTButtonBroadcastReceiver receiver,
                                       FakeContext context,
                                       Intent intent,
                                       boolean expectedPressed,
                                       String message) {
        VoipBackgroundService.resetTestState();

        receiver.onReceive(context, intent);

        assertEquals(1, VoipBackgroundService.dispatchCount, message + " dispatch count");
        assertEquals(expectedPressed, VoipBackgroundService.lastPttPressed, message + " pressed state");
        assertSame(context, VoipBackgroundService.lastContext, message + " context");
    }

    private static void assertEquals(int expected, int actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }

    private static void assertEquals(boolean expected, boolean actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void assertFalse(boolean condition, String message) {
        if (condition) {
            throw new AssertionError(message);
        }
    }

    private static void assertSame(Object expected, Object actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected same instance");
        }
    }
}
