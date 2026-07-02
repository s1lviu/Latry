package yo6say.latry;

import android.content.Intent;

public final class NotificationActionReceiverSelfTest {
    private NotificationActionReceiverSelfTest() {
    }

    public static void main(String[] args) {
        testDisconnectActionStopsService();
        testUnknownActionIsIgnored();
        testNullActionIsIgnored();
        System.out.println("NotificationActionReceiverSelfTest passed");
    }

    private static void testDisconnectActionStopsService() {
        FakeContext context = new FakeContext();
        NotificationActionReceiver receiver = new NotificationActionReceiver();
        VoipBackgroundService.resetTestState();

        receiver.onReceive(context, new Intent("ACTION_DISCONNECT"));

        assertEquals(1, VoipBackgroundService.stopCount, "disconnect action should stop the service");
        assertSame(context, VoipBackgroundService.lastContext, "disconnect action should pass through the receiver context");
    }

    private static void testUnknownActionIsIgnored() {
        FakeContext context = new FakeContext();
        NotificationActionReceiver receiver = new NotificationActionReceiver();
        VoipBackgroundService.resetTestState();

        receiver.onReceive(context, new Intent("ACTION_UNKNOWN"));

        assertEquals(0, VoipBackgroundService.stopCount, "unknown action should be ignored");
    }

    private static void testNullActionIsIgnored() {
        FakeContext context = new FakeContext();
        NotificationActionReceiver receiver = new NotificationActionReceiver();
        VoipBackgroundService.resetTestState();

        receiver.onReceive(context, new Intent());

        assertEquals(0, VoipBackgroundService.stopCount, "null action should be ignored");
    }

    private static void assertEquals(int expected, int actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }

    private static void assertSame(Object expected, Object actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected same instance");
        }
    }
}
