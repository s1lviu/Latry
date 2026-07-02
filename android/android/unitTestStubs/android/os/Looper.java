package android.os;

public final class Looper {
    private static final Looper mainLooper = new Looper();

    private Looper() {
    }

    public static Looper getMainLooper() {
        return mainLooper;
    }
}
