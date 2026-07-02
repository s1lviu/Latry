package android.content;

public abstract class BroadcastReceiver {
    private boolean orderedBroadcast = false;
    private boolean broadcastAborted = false;

    public abstract void onReceive(Context context, Intent intent);

    public boolean isOrderedBroadcast() {
        return orderedBroadcast;
    }

    public void abortBroadcast() {
        broadcastAborted = true;
    }

    public void setOrderedBroadcastForTesting(boolean ordered) {
        orderedBroadcast = ordered;
        broadcastAborted = false;
    }

    public boolean wasAbortBroadcastCalledForTesting() {
        return broadcastAborted;
    }
}
