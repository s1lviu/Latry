package yo6say.latry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;
import android.view.KeyEvent;

public final class PTTButtonBroadcastReceiver extends BroadcastReceiver {
    public static final String ACTION_PTT = "android.intent.action.PTT";
    public static final String ACTION_PTT_DOWN = "android.intent.action.PTT_DOWN";
    public static final String ACTION_PTT_KEY = "android.intent.action.PTT_KEY";
    public static final String ACTION_PTT_UP = "android.intent.action.PTT_UP";
    public static final String ACTION_PTT_DOWN_ALT = "PTT.down";
    public static final String ACTION_PTT_UP_ALT = "PTT.up";
    static final String ACTION_MEIG_KEY_EVENT = "com.meigsmart.meigkeyaccessibility.onkeyevent";

    private static final String TAG = "LatryPTTReceiver";
    private static final String EXTRA_KEYCODE = "keycode";
    private static final String EXTRA_KEYCODE_ALT = "keyCode";
    private static final String EXTRA_KEY = "key";
    private static final String EXTRA_ACTION = "action";
    private static final String EXTRA_KEY_OPERATION = "keyOperation";
    static final int MEIG_PTT_KEYCODE = 142;
    private static final int MEIG_ACTION_DOWN = 0;
    private static final int MEIG_ACTION_UP = 1;

    @Override
    public void onReceive(Context context, Intent intent) {
        if (context == null || intent == null) {
            return;
        }

        final String action = intent.getAction();
        Log.i(TAG, "PTT/media button broadcast received: " + action);
        if (action == null) {
            return;
        }

        switch (action) {
        case ACTION_PTT_DOWN:
        case ACTION_PTT_DOWN_ALT:
            dispatchExternalPttAction(context, true, action);
            return;
        case VoipBackgroundService.ACTION_PTT_PRESSED:
            VoipBackgroundService.dispatchPTTAction(context, true);
            return;
        case ACTION_PTT_UP:
        case ACTION_PTT_UP_ALT:
            dispatchExternalPttAction(context, false, action);
            return;
        case VoipBackgroundService.ACTION_PTT_RELEASED:
            VoipBackgroundService.dispatchPTTAction(context, false);
            return;
        case ACTION_PTT:
        case ACTION_PTT_KEY:
            final boolean pressed = intent.getBooleanExtra("pressed", false)
                    || intent.getBooleanExtra("state", false);
            dispatchExternalPttAction(context, pressed, action);
            return;
        case ACTION_MEIG_KEY_EVENT:
            handleMeigKeyEvent(context, intent);
            return;
        case Intent.ACTION_MEDIA_BUTTON:
            handleMediaButton(context, intent);
            return;
        default:
            Log.w(TAG, "Ignoring unsupported PTT/media action: " + action);
            return;
        }
    }

    private void handleMediaButton(Context context, Intent intent) {
        final KeyEvent keyEvent = extractKeyEvent(intent);
        if (keyEvent == null) {
            Log.w(TAG, "MEDIA_BUTTON broadcast without KeyEvent");
            return;
        }

        final int keyCode = keyEvent.getKeyCode();
        final int action = keyEvent.getAction();
        Log.i(TAG, "Media button keyCode=" + KeyEvent.keyCodeToString(keyCode)
                + " action=" + action);

        if (HardwarePttLearningCoordinator.isLearningActive()) {
            if (HardwarePttLearningCoordinator.tryConsumeCandidateKey(context, action, keyCode)) {
                Log.i(TAG, "Media button consumed by PTT learning: keyCode="
                        + KeyEvent.keyCodeToString(keyCode) + " action=" + action);
                return;
            }
        }

        if (keyCode == KeyEvent.KEYCODE_HEADSETHOOK || keyCode == KeyEvent.KEYCODE_CALL) {
            if (action == KeyEvent.ACTION_DOWN) {
                dispatchExternalPttAction(context, true, Intent.ACTION_MEDIA_BUTTON);
            } else if (action == KeyEvent.ACTION_UP) {
                dispatchExternalPttAction(context, false, Intent.ACTION_MEDIA_BUTTON);
            }
            return;
        }

        if (HardwarePttKeyPolicy.shouldHandleKeyEvent(
                context,
                Build.BRAND,
                Build.MANUFACTURER,
                Build.MODEL,
                Build.DEVICE,
                Build.PRODUCT,
                action,
                keyCode)) {
            Log.i(TAG, "Fixed hardware media-button PTT keyCode=" + KeyEvent.keyCodeToString(keyCode)
                    + " action=" + action);
            dispatchExternalPttAction(context, action == KeyEvent.ACTION_DOWN, Intent.ACTION_MEDIA_BUTTON);
            return;
        }

        Log.d(TAG, "Ignoring non-PTT media button keyCode=" + KeyEvent.keyCodeToString(keyCode));
    }

    private void handleMeigKeyEvent(Context context, Intent intent) {
        if (shouldDeferMeigHandlingToService(context)) {
            Log.d(TAG, "Ignoring Meig PTT broadcast in activity context because service receiver is active");
            return;
        }

        final Integer keyCode = getFirstIntExtra(intent, EXTRA_KEYCODE, EXTRA_KEYCODE_ALT, EXTRA_KEY);
        if (keyCode == null) {
            Log.w(TAG, "Ignoring Meig PTT broadcast without keycode");
            return;
        }

        if (keyCode.intValue() != MEIG_PTT_KEYCODE) {
            Log.d(TAG, "Ignoring Meig non-PTT keyCode=" + keyCode);
            return;
        }

        final Integer keyAction = getFirstIntExtra(intent, EXTRA_ACTION, EXTRA_KEY_OPERATION);
        if (keyAction == null) {
            Log.w(TAG, "Ignoring Meig PTT broadcast without action for keyCode=" + keyCode);
            return;
        }

        switch (keyAction.intValue()) {
        case MEIG_ACTION_DOWN:
            Log.i(TAG, "Meig PTT DOWN received");
            dispatchExternalPttAction(context, true, ACTION_MEIG_KEY_EVENT);
            return;
        case MEIG_ACTION_UP:
            Log.i(TAG, "Meig PTT UP received");
            dispatchExternalPttAction(context, false, ACTION_MEIG_KEY_EVENT);
            return;
        default:
            Log.w(TAG, "Ignoring Meig PTT broadcast with unsupported action=" + keyAction);
            return;
        }
    }

    private boolean shouldDeferMeigHandlingToService(Context context) {
        return context instanceof LatryActivity && VoipBackgroundService.isRunning();
    }

    private void dispatchExternalPttAction(Context context, boolean pressed, String action) {
        VoipBackgroundService.dispatchPTTAction(context, pressed);
        maybeAbortOrderedHardwarePttBroadcast(action);
    }

    private void maybeAbortOrderedHardwarePttBroadcast(String action) {
        if (!isOrderedBroadcast()) {
            return;
        }

        if (!VoipBackgroundService.shouldClaimOrderedPttBroadcasts()) {
            Log.d(TAG, "Leaving ordered PTT broadcast unclaimed because Latry is not connected: " + action);
            return;
        }

        abortBroadcast();
        Log.i(TAG, "Claimed ordered hardware PTT broadcast exclusively: " + action);
    }

    private Integer getFirstIntExtra(Intent intent, String... names) {
        for (String name : names) {
            if (!intent.hasExtra(name)) {
                continue;
            }

            return Integer.valueOf(intent.getIntExtra(name, 0));
        }

        return null;
    }

    private KeyEvent extractKeyEvent(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT, KeyEvent.class);
        }
        return getParcelableExtraCompat(intent);
    }

    @SuppressWarnings("deprecation")
    private KeyEvent getParcelableExtraCompat(Intent intent) {
        return intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
    }
}
