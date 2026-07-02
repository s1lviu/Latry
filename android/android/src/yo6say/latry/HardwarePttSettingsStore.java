package yo6say.latry;

import android.content.Context;
import android.content.SharedPreferences;

final class HardwarePttSettingsStore {
    static final int NO_LEARNED_PTT_KEY_CODE = -1;

    private static final String PREFS_NAME = "LatryHardwarePttPrefs";
    private static final String PREF_POC_BUTTON_ENABLED = "poc_button_enabled";
    private static final String PREF_LEARNED_PTT_KEY_CODE = "learned_ptt_key_code";

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

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
}
