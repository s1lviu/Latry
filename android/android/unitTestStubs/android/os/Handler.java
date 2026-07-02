package android.os;

/**
 * Minimal stub for unit tests. postDelayed stores the runnable but
 * never fires it automatically — tests must call {@link #flush()} to
 * execute pending runnables, simulating the timeout.
 */
public class Handler {
    private Runnable pendingRunnable;
    private long pendingDelayMs;

    public Handler(Looper looper) {
    }

    public boolean postDelayed(Runnable r, long delayMillis) {
        pendingRunnable = r;
        pendingDelayMs = delayMillis;
        return true;
    }

    public void removeCallbacks(Runnable r) {
        if (pendingRunnable == r) {
            pendingRunnable = null;
        }
    }

    /** Test helper: run the pending runnable immediately (simulates timeout). */
    public void flush() {
        Runnable r = pendingRunnable;
        pendingRunnable = null;
        if (r != null) {
            r.run();
        }
    }

    /** Test helper: returns the pending delay in milliseconds. */
    public long pendingDelayMs() {
        return pendingDelayMs;
    }
}
