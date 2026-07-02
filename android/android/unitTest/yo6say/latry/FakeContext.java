package yo6say.latry;

import android.app.NotificationManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import java.util.HashMap;
import java.util.Map;

final class FakeContext extends Context {
    private final Map<String, SharedPreferences> sharedPreferences = new HashMap<>();
    private final NotificationManager notificationManager = new NotificationManager();
    private final ApplicationInfo applicationInfo = new ApplicationInfo();

    FakeContext() {
        applicationInfo.packageName = "yo6say.latry";
        applicationInfo.targetSdkVersion = 36;
    }

    @Override
    public SharedPreferences getSharedPreferences(String name, int mode) {
        SharedPreferences preferences = sharedPreferences.get(name);
        if (preferences == null) {
            preferences = new FakeSharedPreferences();
            sharedPreferences.put(name, preferences);
        }
        return preferences;
    }

    @Override
    public Object getSystemService(String name) {
        if (Context.NOTIFICATION_SERVICE.equals(name)) {
            return notificationManager;
        }
        return null;
    }

    @Override
    public ApplicationInfo getApplicationInfo() {
        return applicationInfo;
    }

    @Override
    public String getPackageName() {
        return applicationInfo.packageName;
    }

    NotificationManager notificationManager() {
        return notificationManager;
    }

    void setTargetSdkVersion(int targetSdkVersion) {
        applicationInfo.targetSdkVersion = targetSdkVersion;
    }
}
