package android.content;

import android.content.pm.ApplicationInfo;

public abstract class Context {
    public static final int MODE_PRIVATE = 0;
    public static final String NOTIFICATION_SERVICE = "notification";

    public abstract SharedPreferences getSharedPreferences(String name, int mode);

    public Object getSystemService(String name) {
        return null;
    }

    public ApplicationInfo getApplicationInfo() {
        return null;
    }

    public String getPackageName() {
        return "";
    }
}
