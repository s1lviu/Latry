package yo6say.latry;

final class VoipServiceLifecyclePolicy {
    private VoipServiceLifecyclePolicy() {
    }

    static boolean shouldRemainForeground(boolean connected,
                                          boolean receiving,
                                          boolean transmitting,
                                          boolean activityVisible) {
        return connected || receiving || transmitting || !activityVisible;
    }
}
