package yo6say.latry;

public final class VoipServiceLifecyclePolicySelfTest {
    private VoipServiceLifecyclePolicySelfTest() {
    }

    public static void main(String[] args) {
        testForegroundPolicy();
        System.out.println("VoipServiceLifecyclePolicySelfTest passed");
    }

    private static void testForegroundPolicy() {
        assertEquals(true,
                VoipServiceLifecyclePolicy.shouldRemainForeground(true, false, false, true),
                "connected state should stay foreground");
        assertEquals(true,
                VoipServiceLifecyclePolicy.shouldRemainForeground(false, true, false, true),
                "receiving state should stay foreground");
        assertEquals(true,
                VoipServiceLifecyclePolicy.shouldRemainForeground(false, false, true, true),
                "transmitting state should stay foreground");
        assertEquals(false,
                VoipServiceLifecyclePolicy.shouldRemainForeground(false, false, false, true),
                "idle disconnected state should not stay foreground while UI is visible");
        assertEquals(true,
                VoipServiceLifecyclePolicy.shouldRemainForeground(false, false, false, false),
                "idle disconnected state should stay foreground when the UI is not visible");
    }

    private static void assertEquals(boolean expected, boolean actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
