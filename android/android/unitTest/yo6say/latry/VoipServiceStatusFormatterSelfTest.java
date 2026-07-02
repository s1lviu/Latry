package yo6say.latry;

public final class VoipServiceStatusFormatterSelfTest {
    private VoipServiceStatusFormatterSelfTest() {
    }

    public static void main(String[] args) {
        testConnectingStatus();
        testDisconnectedStatus();
        testNotificationTitle();
        testNotificationText();
        testDetailedNotificationText();
        System.out.println("VoipServiceStatusFormatterSelfTest passed");
    }

    private static void testConnectingStatus() {
        assertEquals("Connecting...", VoipServiceStatusFormatter.buildConnectingStatus("", 0),
            "empty endpoint should use generic connecting status");
        assertEquals("Connecting to reflector-test.brainic.ro:5337",
            VoipServiceStatusFormatter.buildConnectingStatus("reflector-test.brainic.ro", 5337),
            "valid endpoint should be included in connecting status");
    }

    private static void testDisconnectedStatus() {
        assertEquals("Android Auto ready",
            VoipServiceStatusFormatter.buildDisconnectedStatus("", 0),
            "missing reconnect profile should show Android Auto ready");
        assertEquals("Ready to connect to reflector-test.brainic.ro:5337",
            VoipServiceStatusFormatter.buildDisconnectedStatus("reflector-test.brainic.ro", 5337),
            "saved endpoint should be reflected in disconnected status");
    }

    private static void testNotificationTitle() {
        assertEquals("Latry TX Active",
            VoipServiceStatusFormatter.buildNotificationTitle(true, true, true),
            "TX should have highest precedence");
        assertEquals("Latry RX Active",
            VoipServiceStatusFormatter.buildNotificationTitle(true, true, false),
            "RX should win when not transmitting");
        assertEquals("Latry Monitoring",
            VoipServiceStatusFormatter.buildNotificationTitle(true, false, false),
            "connected idle state should show monitoring");
        assertEquals("Latry Ready",
            VoipServiceStatusFormatter.buildNotificationTitle(false, false, false),
            "disconnected state should show ready");
    }

    private static void testNotificationText() {
        assertEquals("Transmitting on TG 9",
            VoipServiceStatusFormatter.buildNotificationText(true, false, true, 9, "", "Connected",
                "reflector-test.brainic.ro", 5337),
            "TX text should include talkgroup");
        assertEquals("Receiving YO6ABC",
            VoipServiceStatusFormatter.buildNotificationText(true, true, false, 9, "YO6ABC", "Connected",
                "reflector-test.brainic.ro", 5337),
            "RX text should include the current talker");
        assertEquals("Receiving audio",
            VoipServiceStatusFormatter.buildNotificationText(true, true, false, 9, "", "Connected",
                "reflector-test.brainic.ro", 5337),
            "RX text should fall back when no talker is known");
        assertEquals("Monitoring TG 9",
            VoipServiceStatusFormatter.buildNotificationText(true, false, false, 9, "", "Connected",
                "reflector-test.brainic.ro", 5337),
            "connected text should include talkgroup when available");
        assertEquals("Connected",
            VoipServiceStatusFormatter.buildNotificationText(true, false, false, 0, "", "Connected",
                "reflector-test.brainic.ro", 5337),
            "connected text should fall back to a generic label without talkgroup");
        assertEquals("Reconnecting",
            VoipServiceStatusFormatter.buildNotificationText(false, false, false, 0, "", "Reconnecting",
                "reflector-test.brainic.ro", 5337),
            "disconnected text should preserve non-empty connection status");
        assertEquals("Ready to connect to reflector-test.brainic.ro:5337",
            VoipServiceStatusFormatter.buildNotificationText(false, false, false, 0, "", "",
                "reflector-test.brainic.ro", 5337),
            "empty connection status should fall back to saved endpoint");
    }

    private static void testDetailedNotificationText() {
        assertEquals("Connected | YO6SAY | TG 9 | TX",
            VoipServiceStatusFormatter.buildDetailedNotificationText("Connected", "YO6SAY", 9, false, true, ""),
            "detailed text should append callsign, talkgroup, and TX state");
        assertEquals("Connected | YO6SAY | TG 9 | RX YO6ABC",
            VoipServiceStatusFormatter.buildDetailedNotificationText("Connected", "YO6SAY", 9, true, false, "YO6ABC"),
            "detailed text should append the current talker during RX");
    }

    private static void assertEquals(String expected, String actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
