package androidx.core.content;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.IntentFilter;
import android.os.Handler;

public final class ContextCompat {
    public static final int RECEIVER_NOT_EXPORTED = 4;

    private ContextCompat() {
    }

    public static void registerReceiver(Context context,
                                        BroadcastReceiver receiver,
                                        IntentFilter filter,
                                        String broadcastPermission,
                                        Handler scheduler,
                                        int flags) {
    }
}
