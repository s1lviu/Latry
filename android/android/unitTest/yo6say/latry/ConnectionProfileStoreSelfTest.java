package yo6say.latry;

import android.content.Context;

public final class ConnectionProfileStoreSelfTest {
    private ConnectionProfileStoreSelfTest() {
    }

    public static void main(String[] args) {
        testSaveLoadAndClearConnectionState();
        testClearDoesNotRemoveProfile();
        System.out.println("ConnectionProfileStoreSelfTest passed");
    }

    private static void testSaveLoadAndClearConnectionState() {
        FakeContext context = new FakeContext();

        ConnectionProfileStore.saveConnectionState(context, "reflector.example.net", 5300, "YO6SAY", 9, "secret", "112++,3100", 45);

        assertTrue(ConnectionProfileStore.wasConnectedBeforeRestart(context), "connection flag should be set");
        assertTrue(ConnectionProfileStore.hasSavedConnectionProfile(context), "saved profile should be complete");
        assertEquals("reflector.example.net", ConnectionProfileStore.getSavedHost(context), "saved host should match");
        assertEquals(5300, ConnectionProfileStore.getSavedPort(context), "saved port should match");
        assertEquals("YO6SAY", ConnectionProfileStore.getSavedCallsign(context), "saved callsign should match");
        assertEquals(9, ConnectionProfileStore.getSavedTalkgroup(context), "saved talkgroup should match");
        assertEquals("secret", ConnectionProfileStore.getSavedAuthKey(context), "saved auth key should match");
        assertEquals("112++,3100", ConnectionProfileStore.getSavedMonitoredTalkgroups(context), "saved monitored talkgroups should match");
        assertEquals(45, ConnectionProfileStore.getSavedTgSelectTimeout(context), "saved TG select timeout should match");
    }

    private static void testClearDoesNotRemoveProfile() {
        FakeContext context = new FakeContext();

        ConnectionProfileStore.saveConnectionState(context, "reflector.example.net", 5300, "YO6SAY", 9, "secret", "112++,3100", 45);
        ConnectionProfileStore.clearConnectionState(context);

        assertFalse(ConnectionProfileStore.wasConnectedBeforeRestart(context), "clear should reset the transient flag");
        assertTrue(ConnectionProfileStore.hasSavedConnectionProfile(context), "saved reconnect profile should remain available");
        assertEquals("reflector.example.net", ConnectionProfileStore.getSavedHost(context), "saved host should remain after clear");
    }

    private static void assertTrue(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void assertFalse(boolean value, String message) {
        if (value) {
            throw new AssertionError(message);
        }
    }

    private static void assertEquals(String expected, String actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }

    private static void assertEquals(int expected, int actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
