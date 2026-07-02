package yo6say.latry;

import android.content.Context;

final class HardwarePttKeyPolicy {
    static final int ACTION_DOWN = 0;
    static final int ACTION_UP = 1;

    static final int KEYCODE_MEDIA_PREVIOUS = 88;
    static final int KEYCODE_BUTTON_A = 96;
    static final int KEYCODE_BUTTON_B = 97;
    static final int KEYCODE_BUTTON_C = 98;
    static final int KEYCODE_BUTTON_L1 = 102;
    static final int KEYCODE_BUTTON_R1 = 103;
    static final int KEYCODE_VENDOR_PTT = 142;
    static final int KEYCODE_CHANNEL_UP = 261;
    static final int KEYCODE_CHANNEL_DOWN = 262;

    private HardwarePttKeyPolicy() {
    }

    static boolean shouldHandleKeyEvent(Context context,
                                        String brand,
                                        String manufacturer,
                                        String model,
                                        String device,
                                        String product,
                                        int action,
                                        int keyCode) {
        if (action != ACTION_DOWN && action != ACTION_UP) {
            return false;
        }

        if (isAlwaysSupportedKeyCode(keyCode)) {
            return true;
        }

        if (matchesLearnedKey(context, keyCode)) {
            return true;
        }

        return HardwarePttSettingsStore.isPocButtonEnabled(context)
                && isLearnableHardwareKeyCode(keyCode);
    }

    static boolean isAlwaysSupportedKeyCode(int keyCode) {
        switch (keyCode) {
        case KEYCODE_MEDIA_PREVIOUS:
        case KEYCODE_BUTTON_A:
        case KEYCODE_BUTTON_B:
        case KEYCODE_BUTTON_C:
        case KEYCODE_BUTTON_L1:
        case KEYCODE_BUTTON_R1:
        case KEYCODE_VENDOR_PTT:
        case KEYCODE_CHANNEL_UP:
        case KEYCODE_CHANNEL_DOWN:
            return true;
        default:
            return false;
        }
    }

    static boolean matchesLearnedKey(Context context, int keyCode) {
        return keyCode == HardwarePttSettingsStore.getLearnedPttKeyCode(context);
    }

    /**
     * Returns true when keyCode is a valid candidate during key learning.
     * Rejects obvious system, navigation, volume, and digit keys that should
     * never be bound to PTT.
     */
    static boolean isLearnableHardwareKeyCode(int keyCode) {
        if (keyCode <= 0) {
            return false;
        }

        switch (keyCode) {
        // Soft / system keys
        case 3:   // HOME
        case 4:   // BACK
        case 5:   // CALL
        case 6:   // ENDCALL
        // Digits 0-9
        case 7:   // 0
        case 8:   // 1
        case 9:   // 2
        case 10:  // 3
        case 11:  // 4
        case 12:  // 5
        case 13:  // 6
        case 14:  // 7
        case 15:  // 8
        case 16:  // 9
        // D-pad cluster
        case 19:  // DPAD_UP
        case 20:  // DPAD_DOWN
        case 21:  // DPAD_LEFT
        case 22:  // DPAD_RIGHT
        case 23:  // DPAD_CENTER
        // Volume
        case 24:  // VOLUME_UP
        case 25:  // VOLUME_DOWN
        case 164: // VOLUME_MUTE
        // Power, camera, focus
        case 26:  // POWER
        case 27:  // CAMERA
        case 28:  // CAMERA / FOCUS (legacy)
        case 80:  // FOCUS
        // Enter, delete, tab, space, escape
        case 66:  // ENTER
        case 67:  // DEL
        case 61:  // TAB
        case 62:  // SPACE
        case 111: // ESCAPE
        // Menu, search, notification
        case 82:  // MENU
        case 84:  // SEARCH
        case 83:  // NOTIFICATION
        // System navigation
        case 187: // APP_SWITCH
        case 219: // ASSIST
        case 220: // BRIGHTNESS_DOWN
        case 221: // BRIGHTNESS_UP
        case 223: // SLEEP
        case 224: // WAKEUP
        case 280: // SYSTEM_NAVIGATION_UP
        case 281: // SYSTEM_NAVIGATION_DOWN
        case 282: // SYSTEM_NAVIGATION_LEFT
            return false;
        default:
            return true;
        }
    }
}
