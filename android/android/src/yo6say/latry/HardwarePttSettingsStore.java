package yo6say.latry;

import android.content.Context;
import android.content.SharedPreferences;

final class HardwarePttSettingsStore {
    static final int NO_LEARNED_PTT_KEY_CODE = -1;

    private static final String PREFS_NAME = "LatryHardwarePttPrefs";
    private static final String PREF_POC_BUTTON_ENABLED = "poc_button_enabled";
    private static final String PREF_LEARNED_PTT_KEY_CODE = "learned_ptt_key_code";
    static final String NO_LEARNED_SPP_ADDRESS = "";
    private static final String PREF_LEARNED_SPP_ADDRESS        = "learned_spp_address";
    private static final String PREF_LEARNED_SPP_NAME           = "learned_spp_name";
    private static final String PREF_LEARNED_SPP_PRESS_PATTERN  = "learned_spp_press_pattern";
    private static final String PREF_LEARNED_SPP_RELEASE_PATTERN = "learned_spp_release_pattern";

    private HardwarePttSettingsStore() {
    }

    static boolean isPocButtonEnabled(Context context) {
        if (context == null) {
            return false;
        }

        return preferences(context).getBoolean(PREF_POC_BUTTON_ENABLED, false);
    }

    static void setPocButtonEnabled(Context context, boolean enabled) {
        if (context == null) {
            return;
        }

        preferences(context).edit()
                .putBoolean(PREF_POC_BUTTON_ENABLED, enabled)
                .apply();
    }

    static boolean hasLearnedPttKeyCode(Context context) {
        return getLearnedPttKeyCode(context) != NO_LEARNED_PTT_KEY_CODE;
    }

    static int getLearnedPttKeyCode(Context context) {
        if (context == null) {
            return NO_LEARNED_PTT_KEY_CODE;
        }

        final int keyCode = preferences(context).getInt(
                PREF_LEARNED_PTT_KEY_CODE,
                NO_LEARNED_PTT_KEY_CODE);
        return keyCode > 0 ? keyCode : NO_LEARNED_PTT_KEY_CODE;
    }

    static void setLearnedPttKeyCode(Context context, int keyCode) {
        if (context == null) {
            return;
        }

        preferences(context).edit()
                .putInt(PREF_LEARNED_PTT_KEY_CODE,
                        keyCode > 0 ? keyCode : NO_LEARNED_PTT_KEY_CODE)
                .apply();
    }

    static void clearLearnedPttKeyCode(Context context) {
        if (context == null) {
            return;
        }

        preferences(context).edit()
                .putInt(PREF_LEARNED_PTT_KEY_CODE, NO_LEARNED_PTT_KEY_CODE)
                .apply();
    }

    static boolean hasLearnedSppDevice(Context context) {
        return !getLearnedSppAddress(context).isEmpty();
    }

    static String getLearnedSppAddress(Context context) {
        if (context == null) {
            return NO_LEARNED_SPP_ADDRESS;
        }
        return preferences(context).getString(PREF_LEARNED_SPP_ADDRESS, NO_LEARNED_SPP_ADDRESS);
    }

    static String getLearnedSppName(Context context) {
        if (context == null) {
            return "";
        }
        return preferences(context).getString(PREF_LEARNED_SPP_NAME, "");
    }

    static String getLearnedSppPressPattern(Context context) {
        if (context == null) {
            return "";
        }
        return preferences(context).getString(PREF_LEARNED_SPP_PRESS_PATTERN, "");
    }
 
    static String getLearnedSppReleasePattern(Context context) {
        if (context == null) {
            return "";
        }
        return preferences(context).getString(PREF_LEARNED_SPP_RELEASE_PATTERN, "");
    }

    static void setLearnedSppDevice(Context context, String name, String address,
                                    String pressPattern, String releasePattern) {
        if (context == null) {
            return;
        }
        preferences(context).edit()
                .putString(PREF_LEARNED_SPP_ADDRESS, address != null ? address : NO_LEARNED_SPP_ADDRESS)
                .putString(PREF_LEARNED_SPP_NAME, name != null ? name : "")
                .putString(PREF_LEARNED_SPP_PRESS_PATTERN, pressPattern != null ? pressPattern : "")
                .putString(PREF_LEARNED_SPP_RELEASE_PATTERN, releasePattern != null ? releasePattern : "")
                .apply();
    }

    static void clearLearnedSppDevice(Context context) {
        if (context == null) {
            return;
        }
        preferences(context).edit()
                .putString(PREF_LEARNED_SPP_ADDRESS, NO_LEARNED_SPP_ADDRESS)
                .putString(PREF_LEARNED_SPP_NAME, "")
                .putString(PREF_LEARNED_SPP_PRESS_PATTERN, "")
                .putString(PREF_LEARNED_SPP_RELEASE_PATTERN, "")
                .apply();
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
}
