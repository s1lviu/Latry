package android.app;

import android.content.Context;

public class Notification {
    public CharSequence contentTitle;
    public CharSequence contentText;
    public int smallIcon;
    public PendingIntent contentIntent;
    public boolean autoCancel;

    public static class Builder {
        private final Notification notification = new Notification();

        public Builder(Context context, String channelId) {
        }

        public Builder setContentTitle(CharSequence title) {
            notification.contentTitle = title;
            return this;
        }

        public Builder setContentText(CharSequence text) {
            notification.contentText = text;
            return this;
        }

        public Builder setSmallIcon(int icon) {
            notification.smallIcon = icon;
            return this;
        }

        public Builder setContentIntent(PendingIntent intent) {
            notification.contentIntent = intent;
            return this;
        }

        public Builder setAutoCancel(boolean autoCancel) {
            notification.autoCancel = autoCancel;
            return this;
        }

        public Notification build() {
            return notification;
        }
    }
}
