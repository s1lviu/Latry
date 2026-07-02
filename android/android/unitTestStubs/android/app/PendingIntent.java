package android.app;

import android.content.Context;
import android.content.Intent;

public final class PendingIntent {
    public static final int FLAG_UPDATE_CURRENT = 1;
    public static final int FLAG_IMMUTABLE = 1 << 1;

    public final Context context;
    public final int requestCode;
    public final Intent intent;
    public final int flags;

    private PendingIntent(Context context, int requestCode, Intent intent, int flags) {
        this.context = context;
        this.requestCode = requestCode;
        this.intent = intent;
        this.flags = flags;
    }

    public static PendingIntent getForegroundService(Context context, int requestCode,
                                                     Intent intent, int flags) {
        return new PendingIntent(context, requestCode, intent, flags);
    }
}
