package yo6say.latry;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Minimal stub for unit tests. Captures learning result callbacks
 * that {@link HardwarePttLearningCoordinator} dispatches.
 */
public class LatryActivity extends Context {
    public static int lastLearningResult = -1;
    public static int lastLearningKeyCode = -1;

    private LatryActivity() {
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        return null;
    }

    public static void resetTestState() {
        lastLearningResult = -1;
        lastLearningKeyCode = -1;
    }

    static void notifyHardwarePttLearningResult(int result, int keyCode) {
        lastLearningResult = result;
        lastLearningKeyCode = keyCode;
    }
}
