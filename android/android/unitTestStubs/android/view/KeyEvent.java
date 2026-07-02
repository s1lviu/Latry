package android.view;

public final class KeyEvent {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;

    public static final int KEYCODE_HEADSETHOOK = 79;
    public static final int KEYCODE_CALL = 5;
    public static final int KEYCODE_MEDIA_PLAY = 126;
    public static final int KEYCODE_MEDIA_PAUSE = 127;
    public static final int KEYCODE_MEDIA_STOP = 86;

    private final int action;
    private final int keyCode;

    public KeyEvent(int action, int keyCode) {
        this.action = action;
        this.keyCode = keyCode;
    }

    public int getAction() {
        return action;
    }

    public int getKeyCode() {
        return keyCode;
    }

    public static String keyCodeToString(int keyCode) {
        switch (keyCode) {
        case KEYCODE_HEADSETHOOK:
            return "KEYCODE_HEADSETHOOK";
        case KEYCODE_CALL:
            return "KEYCODE_CALL";
        case KEYCODE_MEDIA_PLAY:
            return "KEYCODE_MEDIA_PLAY";
        case KEYCODE_MEDIA_PAUSE:
            return "KEYCODE_MEDIA_PAUSE";
        case KEYCODE_MEDIA_STOP:
            return "KEYCODE_MEDIA_STOP";
        default:
            return "KEYCODE_" + keyCode;
        }
    }
}
