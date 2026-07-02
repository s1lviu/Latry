package yo6say.latry;

public final class HardwarePttKeyPolicySelfTest {
    private HardwarePttKeyPolicySelfTest() {
    }

    public static void main(String[] args) {
        testVendorKeyAlwaysMatches();
        testLearnedKeyMatchesWithoutOptInMode();
        testFixedRuggedKeysAlwaysMatch();
        testPocModeAcceptsAnyLearnableKey();
        testPocModeRejectsSystemKeys();
        testUnsupportedActionsAndKeysAreRejected();
        System.out.println("HardwarePttKeyPolicySelfTest passed");
    }

    private static void testVendorKeyAlwaysMatches() {
        FakeContext context = new FakeContext();

        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT),
                "vendor key 142 should match directly");
    }

    private static void testLearnedKeyMatchesWithoutOptInMode() {
        FakeContext context = new FakeContext();
        HardwarePttSettingsStore.setLearnedPttKeyCode(context, HardwarePttKeyPolicy.KEYCODE_BUTTON_B);

        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_BUTTON_B),
                "learned key should match even when rugged mode is disabled");
        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                141),
                "non-standard keys should not match without PoC mode or learning");
    }

    private static void testFixedRuggedKeysAlwaysMatch() {
        FakeContext context = new FakeContext();

        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_UP,
                HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS),
                "media previous should always match as a known PTT key");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "Generic", "Any Device", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_BUTTON_R1),
                "BUTTON_R1 should always match as a known PTT key");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_BUTTON_A),
                "BUTTON_A should always match as a known PTT key");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_CHANNEL_UP),
                "CHANNEL_UP (261) should always match as a known MTK PoC PTT key");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_UP,
                HardwarePttKeyPolicy.KEYCODE_CHANNEL_DOWN),
                "CHANNEL_DOWN (262) should always match as a known MTK PoC PTT key");
    }

    private static void testPocModeAcceptsAnyLearnableKey() {
        FakeContext context = new FakeContext();
        HardwarePttSettingsStore.setPocButtonEnabled(context, true);

        // MTK/vendor keycodes that PoC devices may use for hardware PTT
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                141),
                "non-standard key 141 should match in PoC mode (blocklist approach)");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                130),
                "MEDIA_RECORD (130) should match in PoC mode");
        assertTrue(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                139),
                "F8 (139) should match in PoC mode");
    }

    private static void testPocModeRejectsSystemKeys() {
        FakeContext context = new FakeContext();
        HardwarePttSettingsStore.setPocButtonEnabled(context, true);

        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                24),
                "VOLUME_UP should be rejected even in PoC mode");
        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                4),
                "BACK should be rejected even in PoC mode");
        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                3),
                "HOME should be rejected even in PoC mode");
    }

    private static void testUnsupportedActionsAndKeysAreRejected() {
        FakeContext context = new FakeContext();

        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                2,
                HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT),
                "unsupported actions should be ignored");
        assertFalse(HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                "", "", "", "", "",
                HardwarePttKeyPolicy.ACTION_DOWN,
                141),
                "unsupported key codes should be ignored");
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
}
