package yo6say.latry;

public final class HardwarePttSettingsStoreSelfTest {
    private HardwarePttSettingsStoreSelfTest() {
    }

    public static void main(String[] args) {
        testDefaultsAreDisabledAndUnset();
        testOptInAndLearnedKeyPersistence();
        testClearingLearnedKeyRestoresUnsetState();
        System.out.println("HardwarePttSettingsStoreSelfTest passed");
    }

    private static void testDefaultsAreDisabledAndUnset() {
        FakeContext context = new FakeContext();

        assertFalse(HardwarePttSettingsStore.isPocButtonEnabled(context),
                "hardware PTT mode should default to disabled");
        assertFalse(HardwarePttSettingsStore.hasLearnedPttKeyCode(context),
                "learned key should default to missing");
        assertEquals(HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE,
                HardwarePttSettingsStore.getLearnedPttKeyCode(context),
                "default learned key code should be unset");
    }

    private static void testOptInAndLearnedKeyPersistence() {
        FakeContext context = new FakeContext();

        HardwarePttSettingsStore.setPocButtonEnabled(context, true);
        HardwarePttSettingsStore.setLearnedPttKeyCode(context, HardwarePttKeyPolicy.KEYCODE_BUTTON_C);

        assertTrue(HardwarePttSettingsStore.isPocButtonEnabled(context),
                "hardware PTT mode should persist");
        assertTrue(HardwarePttSettingsStore.hasLearnedPttKeyCode(context),
                "learned key should be marked as present");
        assertEquals(HardwarePttKeyPolicy.KEYCODE_BUTTON_C,
                HardwarePttSettingsStore.getLearnedPttKeyCode(context),
                "learned key code should persist");
    }

    private static void testClearingLearnedKeyRestoresUnsetState() {
        FakeContext context = new FakeContext();
        HardwarePttSettingsStore.setLearnedPttKeyCode(context, HardwarePttKeyPolicy.KEYCODE_BUTTON_A);

        HardwarePttSettingsStore.clearLearnedPttKeyCode(context);

        assertFalse(HardwarePttSettingsStore.hasLearnedPttKeyCode(context),
                "clear should remove the learned key");
        assertEquals(HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE,
                HardwarePttSettingsStore.getLearnedPttKeyCode(context),
                "clear should restore the unset learned-key marker");
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

    private static void assertEquals(int expected, int actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
