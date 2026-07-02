package android.app;

public class NotificationManager {
    public static final int IMPORTANCE_LOW = 2;

    public int notifyCount = 0;
    public int cancelCount = 0;
    public int lastNotificationId = -1;
    public Notification lastNotification = null;
    public NotificationChannel lastChannel = null;
    public boolean notificationsEnabled = true;

    public void createNotificationChannel(NotificationChannel channel) {
        lastChannel = channel;
    }

    public void notify(int id, Notification notification) {
        notifyCount += 1;
        lastNotificationId = id;
        lastNotification = notification;
    }

    public void cancel(int id) {
        cancelCount += 1;
        if (lastNotificationId == id) {
            lastNotification = null;
        }
    }

    public boolean areNotificationsEnabled() {
        return notificationsEnabled;
    }
}
