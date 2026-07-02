package yo6say.latry;

public final class HardwarePttLearningCoordinatorSelfTest {
    private HardwarePttLearningCoordinatorSelfTest() {
    }

    public static void main(String[] args) {
        testStartAndCancelLearning();
        testValidKeyIsCaptured();
        testInvalidKeyPassesThroughToSystem();
        testSystemKeysAreRejected();
        testActionUpIsSwallowedDuringLearning();
        testDoubleStartReturnsFalse();
        testCandidateKeyOutsideLearningReturnsFalse();
        testIsLearnableAcceptsHardwareKeys();
        testIsLearnableRejectsSystemKeys();
        System.out.println("HardwarePttLearningCoordinatorSelfTest passed");
    }

    private static void testStartAndCancelLearning() {
        resetAll();

        assertTrue(HardwarePttLearningCoordinator.startLearning(),
                "startLearning should succeed");
        assertTrue(HardwarePttLearningCoordinator.isLearningActive(),
                "should be active after start");

        HardwarePttLearningCoordinator.cancelLearning();
        assertFalse(HardwarePttLearningCoordinator.isLearningActive(),
                "should be inactive after cancel");
        assertEquals(HardwarePttLearningCoordinator.RESULT_CANCELLED,
                LatryActivity.lastLearningResult,
                "cancel should report RESULT_CANCELLED");
        assertEquals(HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE,
                LatryActivity.lastLearningKeyCode,
                "cancel should report no key code");
    }

    private static void testValidKeyIsCaptured() {
        resetAll();

        HardwarePttLearningCoordinator.startLearning();
        FakeContext context = new FakeContext();

        boolean consumed = HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                context,
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS);

        assertTrue(consumed, "valid key should be consumed");
        assertFalse(HardwarePttLearningCoordinator.isLearningActive(),
                "learning should end after capture");
        assertEquals(HardwarePttLearningCoordinator.RESULT_KEY_CAPTURED,
                LatryActivity.lastLearningResult,
                "should report RESULT_KEY_CAPTURED");
        assertEquals(HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS,
                LatryActivity.lastLearningKeyCode,
                "should report the captured key code");
        assertEquals(HardwarePttKeyPolicy.KEYCODE_MEDIA_PREVIOUS,
                HardwarePttSettingsStore.getLearnedPttKeyCode(context),
                "key should be persisted in settings store");
    }

    private static void testInvalidKeyPassesThroughToSystem() {
        resetAll();

        HardwarePttLearningCoordinator.startLearning();
        FakeContext context = new FakeContext();

        // HOME key (3) is not learnable — should pass through so the
        // system still handles it (volume, home, back, etc.)
        boolean consumed = HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                context,
                HardwarePttKeyPolicy.ACTION_DOWN,
                3);

        assertFalse(consumed, "non-learnable key should pass through to system");
        assertTrue(HardwarePttLearningCoordinator.isLearningActive(),
                "learning should remain active after non-learnable key");
        assertEquals(-1, LatryActivity.lastLearningResult,
                "no result should be reported for non-learnable key");
    }

    private static void testSystemKeysAreRejected() {
        resetAll();

        HardwarePttLearningCoordinator.startLearning();
        FakeContext context = new FakeContext();

        int[] systemKeys = {3, 4, 24, 25, 26, 66, 82, 84, 187, 219};
        for (int key : systemKeys) {
            boolean consumed = HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                    context,
                    HardwarePttKeyPolicy.ACTION_DOWN,
                    key);
            assertFalse(consumed, "system key " + key + " should pass through");
            assertTrue(HardwarePttLearningCoordinator.isLearningActive(),
                    "learning should remain active after system key " + key);
        }

        // Now send a valid key — it should finalize
        HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                context,
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT);
        assertFalse(HardwarePttLearningCoordinator.isLearningActive(),
                "learning should end after valid key");
        assertEquals(HardwarePttKeyPolicy.KEYCODE_VENDOR_PTT,
                LatryActivity.lastLearningKeyCode,
                "vendor key should be captured");
    }

    private static void testActionUpIsSwallowedDuringLearning() {
        resetAll();

        HardwarePttLearningCoordinator.startLearning();
        FakeContext context = new FakeContext();

        boolean consumed = HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                context,
                HardwarePttKeyPolicy.ACTION_UP,
                HardwarePttKeyPolicy.KEYCODE_BUTTON_A);

        assertTrue(consumed, "ACTION_UP should be swallowed during learning");
        assertTrue(HardwarePttLearningCoordinator.isLearningActive(),
                "learning should remain active on ACTION_UP");
    }

    private static void testDoubleStartReturnsFalse() {
        resetAll();

        assertTrue(HardwarePttLearningCoordinator.startLearning(),
                "first start should succeed");
        assertFalse(HardwarePttLearningCoordinator.startLearning(),
                "second start should fail while already active");

        HardwarePttLearningCoordinator.resetForTesting();
    }

    private static void testCandidateKeyOutsideLearningReturnsFalse() {
        resetAll();

        FakeContext context = new FakeContext();
        boolean consumed = HardwarePttLearningCoordinator.tryConsumeCandidateKey(
                context,
                HardwarePttKeyPolicy.ACTION_DOWN,
                HardwarePttKeyPolicy.KEYCODE_BUTTON_A);

        assertFalse(consumed, "key should not be consumed when not learning");
    }

    private static void testIsLearnableAcceptsHardwareKeys() {
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(88),
                "MEDIA_PREVIOUS (88) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(96),
                "BUTTON_A (96) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(97),
                "BUTTON_B (97) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(142),
                "vendor key 142 should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(102),
                "BUTTON_L1 (102) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(103),
                "BUTTON_R1 (103) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(131),
                "F1 (131) should be learnable");
        assertTrue(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(300),
                "arbitrary high keycode should be learnable");
    }

    private static void testIsLearnableRejectsSystemKeys() {
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(0),
                "keyCode 0 should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(-1),
                "negative keyCode should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(3),
                "HOME should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(4),
                "BACK should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(24),
                "VOLUME_UP should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(25),
                "VOLUME_DOWN should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(26),
                "POWER should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(66),
                "ENTER should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(82),
                "MENU should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(7),
                "digit 0 should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(16),
                "digit 9 should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(19),
                "DPAD_UP should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(23),
                "DPAD_CENTER should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(164),
                "VOLUME_MUTE should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(187),
                "APP_SWITCH should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(223),
                "SLEEP should be rejected");
        assertFalse(HardwarePttKeyPolicy.isLearnableHardwareKeyCode(224),
                "WAKEUP should be rejected");
    }

    private static void resetAll() {
        HardwarePttLearningCoordinator.resetForTesting();
        LatryActivity.resetTestState();
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

    private static void assertEquals(int expected, int actual, String message) {
        if (expected != actual) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
