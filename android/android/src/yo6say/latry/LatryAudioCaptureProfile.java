package yo6say.latry;

import java.util.Locale;
import java.util.regex.Pattern;

final class LatryAudioCaptureProfile {
    static final int AUDIO_SOURCE_DEFAULT = 0;
    static final int AUDIO_SOURCE_MIC = 1;
    static final int AUDIO_SOURCE_VOICE_RECOGNITION = 6;

    private static final int[] STANDARD_SAMPLE_RATES = {48000, 44100, 32000, 24000, 16000};
    private static final int[] MEDIATEK_POC_SAMPLE_RATES = {48000, 24000, 16000};
    private static final int[] STANDARD_AUDIO_SOURCES = {
            AUDIO_SOURCE_MIC,
            AUDIO_SOURCE_DEFAULT,
            AUDIO_SOURCE_VOICE_RECOGNITION
    };
    private static final int[] MEDIATEK_POC_AUDIO_SOURCES = {
            AUDIO_SOURCE_DEFAULT,
            AUDIO_SOURCE_MIC
    };

    private static final Pattern MT67_POC_PATTERN = Pattern.compile(".*mtk?67[36]\\d.*");
    private static final Pattern EXCLUDED_MT68_69_PATTERN = Pattern.compile(".*mt6[89]\\d{2}.*");
    private static final Pattern EXCLUDED_MT81_82_PATTERN = Pattern.compile(".*mt8[12]\\d{2}.*");

    private LatryAudioCaptureProfile() {
    }

    static boolean usesMediaTekPocProfile(String hardware,
                                          String board,
                                          String manufacturer,
                                          String socModel,
                                          String socManufacturer) {
        return matchesMediaTekPocDetection(hardware, board, manufacturer)
                || matchesMediaTekPocDetection(socModel, board, socManufacturer);
    }

    static int[] audioSources(boolean mediaTekPocProfile) {
        return (mediaTekPocProfile ? MEDIATEK_POC_AUDIO_SOURCES : STANDARD_AUDIO_SOURCES).clone();
    }

    static int[] sampleRates(boolean mediaTekPocProfile) {
        return (mediaTekPocProfile ? MEDIATEK_POC_SAMPLE_RATES : STANDARD_SAMPLE_RATES).clone();
    }

    static String name(boolean mediaTekPocProfile) {
        return mediaTekPocProfile ? "mediatek_poc" : "standard";
    }

    private static boolean matchesMediaTekPocDetection(String hardware,
                                                              String board,
                                                              String manufacturer) {
        String normalizedHardware = normalize(hardware);
        String normalizedBoard = normalize(board);
        String normalizedManufacturer = normalize(manufacturer);

        if (MT67_POC_PATTERN.matcher(normalizedHardware).matches()
                || MT67_POC_PATTERN.matcher(normalizedBoard).matches()) {
            return true;
        }

        if (!normalizedManufacturer.contains("mediatek")) {
            return false;
        }

        if (EXCLUDED_MT68_69_PATTERN.matcher(normalizedHardware).matches()
                || EXCLUDED_MT68_69_PATTERN.matcher(normalizedBoard).matches()
                || EXCLUDED_MT81_82_PATTERN.matcher(normalizedHardware).matches()
                || EXCLUDED_MT81_82_PATTERN.matcher(normalizedBoard).matches()) {
            return false;
        }

        return true;
    }

    private static String normalize(String value) {
        return value == null ? "" : value.toLowerCase(Locale.ROOT);
    }
}
