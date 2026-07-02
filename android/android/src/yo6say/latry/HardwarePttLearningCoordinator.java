package yo6say.latry;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/**
 * Manages the transient runtime state of hardware PTT key learning.
 *
 * All methods must be called on the main (UI) thread. The coordinator
 * is intentionally decoupled from the persistence layer — it writes
 * through to {@link HardwarePttSettingsStore} only on successful capture.
 */
final class HardwarePttLearningCoordinator {
    static final int RESULT_NONE = 0;
    static final int RESULT_KEY_CAPTURED = 1;
    static final int RESULT_TIMEOUT = 2;
    static final int RESULT_CANCELLED = 3;

    private static final String TAG = "LatryPttLearn";
    private static final long LEARNING_TIMEOUT_MS = 15_000;

    private static volatile boolean learningActive;
    private static Handler handler;
    private static Runnable timeoutRunnable;

    private HardwarePttLearningCoordinator() {
    }

    /**
     * Enters learning mode. The next valid candidate key delivered via
     * {@link #tryConsumeCandidateKey} will be persisted and learning
     * mode will end.  If no valid key arrives within 15 seconds the
     * mode ends automatically via timeout.
     *
     * @return true if learning was started, false if already active.
     */
    static boolean startLearning(Context context) {
        if (learningActive) {
            Log.w(TAG, "startLearning called while already active");
            return false;
        }

        learningActive = true;
        handler = new Handler(Looper.getMainLooper());
        timeoutRunnable = new Runnable() {
            @Override
            public void run() {
                if (!learningActive) {
                    return;
                }

                Log.i(TAG, "Learning timed out after " + LEARNING_TIMEOUT_MS + " ms");
                learningActive = false;
                handler = null;
                timeoutRunnable = null;
                SppPttScanner.stopScanning();
                LatryActivity.notifyHardwarePttLearningResult(RESULT_TIMEOUT,
                        HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE);
            }
        };
        handler.postDelayed(timeoutRunnable, LEARNING_TIMEOUT_MS);

        SppPttScanner.startScanning(context, (name, address, pressPattern, releasePattern) -> {
            if (!learningActive) {
                return;
            }
            clearTimeout();
            learningActive = false;
            SppPttScanner.stopScanning();
            Log.i(TAG, "SPP PTT device learned: " + name
                    + " press=[" + pressPattern + "]"
                    + " release=[" + releasePattern + "]");
            LatryActivity.notifyHardwarePttLearningResult(4,
                    HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE);
        });

        Log.i(TAG, "Learning mode started (key + SPP), timeout=" + LEARNING_TIMEOUT_MS + " ms");
        return true;
    }

    /**
     * Cancels learning mode without saving anything.
     */
    static void cancelLearning() {
        if (!learningActive) {
            return;
        }

        clearTimeout();
        learningActive = false;
        Log.i(TAG, "Learning mode cancelled by user");
        LatryActivity.notifyHardwarePttLearningResult(RESULT_CANCELLED,
                HardwarePttSettingsStore.NO_LEARNED_PTT_KEY_CODE);
    }

    static boolean isLearningActive() {
        return learningActive;
    }

    /**
     * Offers a candidate key event to the learning coordinator.
     *
     * @param context Android context for persistence.
     * @param action  KeyEvent action (ACTION_DOWN=0, ACTION_UP=1).
     * @param keyCode Android keyCode from the hardware event.
     * @return true if the event was consumed (either captured or rejected
     *         as invalid but swallowed to prevent propagation during learning).
     */
    static boolean tryConsumeCandidateKey(Context context, int action, int keyCode) {
        if (!learningActive) {
            return false;
        }

        // Let non-learnable keys (volume, home, back, etc.) pass through
        // to the system so the user can still operate the device normally.
        if (!HardwarePttKeyPolicy.isLearnableHardwareKeyCode(keyCode)) {
            Log.d(TAG, "Rejected non-learnable keyCode=" + keyCode);
            return false;
        }

        // Swallow ACTION_UP for learnable keys without finalizing —
        // prevents the key from triggering PTT dispatch during learning.
        if (action != HardwarePttKeyPolicy.ACTION_DOWN) {
            return true;
        }

        // Valid candidate — persist and finish
        clearTimeout();
        learningActive = false;
        SppPttScanner.stopScanning();

        HardwarePttSettingsStore.setLearnedPttKeyCode(context, keyCode);
        Log.i(TAG, "Learned hardware PTT keyCode=" + keyCode);

        LatryActivity.notifyHardwarePttLearningResult(RESULT_KEY_CAPTURED, keyCode);
        return true;
    }

    /** Visible for testing: returns the configured timeout duration. */
    static long learningTimeoutMs() {
        return LEARNING_TIMEOUT_MS;
    }

    /**
     * Resets all state without triggering any native callback.
     * For use in unit tests only.
     */
    static void resetForTesting() {
        clearTimeout();
        learningActive = false;
    }

    private static void clearTimeout() {
        if (handler != null && timeoutRunnable != null) {
            handler.removeCallbacks(timeoutRunnable);
        }
        handler = null;
        timeoutRunnable = null;
    }
}
