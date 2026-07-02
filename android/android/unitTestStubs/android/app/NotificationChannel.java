package android.app;

public class NotificationChannel {
    public final String id;
    public final CharSequence name;
    public final int importance;
    public CharSequence description;

    public NotificationChannel(String id, CharSequence name, int importance) {
        this.id = id;
        this.name = name;
        this.importance = importance;
    }

    public void setDescription(CharSequence description) {
        this.description = description;
    }
}
