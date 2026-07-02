package yo6say.latry;

import android.app.Application;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import androidx.core.content.pm.PackageInfoCompat;
import io.sentry.Breadcrumb;
import io.sentry.Sentry;
import io.sentry.SentryLevel;
import io.sentry.android.core.SentryAndroid;
import java.util.Map;

public final class LatrySentry {
    private static final String DSN =
            "https://a2eb25941d126ce64646790deb7894eb@o4511083897749504.ingest.de.sentry.io/4511083901616208";

    private LatrySentry() {
    }

    public static void init(Application application) {
        SentryAndroid.init(application, options -> {
            options.setDsn(DSN);
            options.setRelease(buildRelease(application));
            options.setEnvironment(buildEnvironment(application));
        });

        final String processName = Application.getProcessName();
        Sentry.configureScope(scope -> {
            scope.setTag("runtime", "qt-android");
            if (processName != null && !processName.isEmpty()) {
                scope.setTag("process_name", processName);
            }
        });

        addBreadcrumb("app.lifecycle", "application_created", SentryLevel.INFO);
    }

    public static void addBreadcrumb(String category, String message, SentryLevel level) {
        addBreadcrumb(category, message, level, null);
    }

    public static void addBreadcrumb(String category,
                                     String message,
                                     SentryLevel level,
                                     Map<String, Object> data) {
        Breadcrumb breadcrumb = new Breadcrumb();
        breadcrumb.setCategory(category);
        breadcrumb.setMessage(message);
        breadcrumb.setLevel(level);
        if (data != null) {
            for (Map.Entry<String, Object> entry : data.entrySet()) {
                breadcrumb.setData(entry.getKey(), entry.getValue());
            }
        }
        Sentry.addBreadcrumb(breadcrumb);
    }

    private static String buildRelease(Context context) {
        final String packageName = context.getPackageName();
        try {
            PackageManager packageManager = context.getPackageManager();
            PackageInfo packageInfo = packageManager.getPackageInfo(packageName, 0);
            long versionCode = PackageInfoCompat.getLongVersionCode(packageInfo);
            String versionName = packageInfo.versionName != null ? packageInfo.versionName : "0";
            return packageName + "@" + versionName + "+" + versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return packageName;
        }
    }

    private static String buildEnvironment(Context context) {
        final ApplicationInfo applicationInfo = context.getApplicationInfo();
        final boolean debuggable = applicationInfo != null
                && (applicationInfo.flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        return debuggable ? "development" : "production";
    }
}
