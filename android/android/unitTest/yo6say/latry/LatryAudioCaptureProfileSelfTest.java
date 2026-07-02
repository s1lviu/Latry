package yo6say.latry;

public final class LatryAudioCaptureProfileSelfTest {
    private LatryAudioCaptureProfileSelfTest() {
    }

    public static void main(String[] args) {
        detectsMtk6739AsMediaTekPoc();
        detectsMtk676xBoardAsMediaTekPoc();
        excludesNewerMediaTekFamilies();
        leavesNonMediaTekBuildsOnStandardProfile();
        mediaTekProfileUsesExpectedSourceAndRateOrder();
        standardProfilePreservesExistingSourceAndRateOrder();
        System.out.println("LatryAudioCaptureProfileSelfTest passed");
    }

    private static void detectsMtk6739AsMediaTekPoc() {
        assertTrue(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "mt6739", "unknown", "Hiroyasu", "", ""),
                "MTK6739 hardware must use the MediaTek PoC profile");
        assertTrue(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "mtk6739", "unknown", "Hiroyasu", "", ""),
                "MTK6739 hardware with MTK spelling must use the MediaTek PoC profile");
    }

    private static void detectsMtk676xBoardAsMediaTekPoc() {
        assertTrue(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "unknown", "MT6761", "Generic", "", ""),
                "MT676x board must use the MediaTek PoC profile");
    }

    private static void excludesNewerMediaTekFamilies() {
        assertFalse(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "mt6885", "mt6885", "MediaTek", "", ""),
                "MT68xx must stay off the PoC profile");
        assertFalse(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "mt8195", "mt8195", "MediaTek", "", ""),
                "MT81xx must stay off the PoC profile");
    }

    private static void leavesNonMediaTekBuildsOnStandardProfile() {
        assertFalse(LatryAudioCaptureProfile.usesMediaTekPocProfile(
                        "qcom", "kona", "Qualcomm", "", ""),
                "Non-MediaTek hardware must use the standard profile");
    }

    private static void mediaTekProfileUsesExpectedSourceAndRateOrder() {
        assertArrayEquals(new int[] {
                        LatryAudioCaptureProfile.AUDIO_SOURCE_DEFAULT,
                        LatryAudioCaptureProfile.AUDIO_SOURCE_MIC
                },
                LatryAudioCaptureProfile.audioSources(true),
                "MediaTek profile source order");
        assertArrayEquals(new int[] {48000, 24000, 16000},
                LatryAudioCaptureProfile.sampleRates(true),
                "MediaTek profile sample rates");
    }

    private static void standardProfilePreservesExistingSourceAndRateOrder() {
        assertArrayEquals(new int[] {
                        LatryAudioCaptureProfile.AUDIO_SOURCE_MIC,
                        LatryAudioCaptureProfile.AUDIO_SOURCE_DEFAULT,
                        LatryAudioCaptureProfile.AUDIO_SOURCE_VOICE_RECOGNITION
                },
                LatryAudioCaptureProfile.audioSources(false),
                "Standard profile source order");
        assertArrayEquals(new int[] {48000, 44100, 32000, 24000, 16000},
                LatryAudioCaptureProfile.sampleRates(false),
                "Standard profile sample rates");
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

    private static void assertArrayEquals(int[] expected, int[] actual, String message) {
        if (expected.length != actual.length) {
            throw new AssertionError(message + ": expected length " + expected.length
                    + " but got " + actual.length);
        }

        for (int i = 0; i < expected.length; i++) {
            if (expected[i] != actual[i]) {
                throw new AssertionError(message + ": expected[" + i + "]=" + expected[i]
                        + " but got " + actual[i]);
            }
        }
    }
}
