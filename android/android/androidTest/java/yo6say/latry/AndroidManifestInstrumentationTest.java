package yo6say.latry;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.os.Build;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.qtproject.qt.android.QtServiceBase;

import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class AndroidManifestInstrumentationTest {
    @After
    public void tearDown() throws Exception {
        AndroidIntegrationTestHooks.setEnabled(false);
        AndroidDeviceTestUtils.ensureServiceStopped();
    }

    @Test
    public void manifestDeclaresExpectedAndroidComponents() throws Exception {
        String packageName = AndroidDeviceTestUtils.targetContext().getPackageName();
        PackageManager packageManager = AndroidDeviceTestUtils.targetContext().getPackageManager();
        PackageInfo packageInfo;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            packageInfo = packageManager.getPackageInfo(packageName,
                    PackageManager.PackageInfoFlags.of(
                            (long) PackageManager.GET_SERVICES
                                    | PackageManager.GET_RECEIVERS
                                    | PackageManager.GET_META_DATA
                                    | PackageManager.GET_PERMISSIONS));
        } else {
            packageInfo = packageManager.getPackageInfo(packageName,
                    PackageManager.GET_SERVICES
                            | PackageManager.GET_RECEIVERS
                            | PackageManager.GET_META_DATA
                            | PackageManager.GET_PERMISSIONS);
        }

        ServiceInfo serviceInfo = findService(packageInfo.services, packageName + ".VoipBackgroundService");
        assertNotNull(serviceInfo);
        assertTrue(serviceInfo.enabled);
        assertFalse(serviceInfo.exported);
        assertEquals(packageName, serviceInfo.processName);
        assertEquals(QtServiceBase.class, VoipBackgroundService.class.getSuperclass());
        assertNotNull(serviceInfo.metaData);
        assertTrue(serviceInfo.metaData.getBoolean("android.app.background_running"));
        assertEquals("latryservice", serviceInfo.metaData.getString("android.app.lib_name"));
        assertEquals("--android-service", serviceInfo.metaData.getString("android.app.arguments"));

        ActivityInfo pttReceiver = findReceiver(packageInfo.receivers, packageName + ".PTTButtonBroadcastReceiver");
        assertNotNull(pttReceiver);
        assertTrue(pttReceiver.enabled);
        assertTrue(pttReceiver.exported);

        ActivityInfo notificationReceiver = findReceiver(packageInfo.receivers, packageName + ".NotificationActionReceiver");
        assertNotNull(notificationReceiver);
        assertFalse(notificationReceiver.exported);

        ActivityInfo bootReceiver = findReceiver(packageInfo.receivers, packageName + ".BootReceiver");
        assertNull("BootReceiver should not be registered (auto-start on boot is disabled)", bootReceiver);

        Intent mediaButtonIntent = new Intent(Intent.ACTION_MEDIA_BUTTON);
        mediaButtonIntent.setPackage(packageName);
        List<ResolveInfo> receivers;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            receivers = packageManager.queryBroadcastReceivers(mediaButtonIntent,
                    PackageManager.ResolveInfoFlags.of(0));
        } else {
            receivers = packageManager.queryBroadcastReceivers(mediaButtonIntent, 0);
        }

        boolean foundPttReceiver = false;
        for (ResolveInfo resolveInfo : receivers) {
            ActivityInfo activityInfo = resolveInfo.activityInfo;
            if (activityInfo != null && (packageName + ".PTTButtonBroadcastReceiver").equals(activityInfo.name)) {
                foundPttReceiver = true;
                break;
            }
        }
        assertTrue(foundPttReceiver);

        assertTrue(hasRequestedPermission(packageInfo, "android.permission.READ_PHONE_STATE"));
        assertTrue(hasRequestedPermission(packageInfo, "android.permission.BLUETOOTH_CONNECT"));
    }

    private static ServiceInfo findService(ServiceInfo[] services, String className) {
        if (services == null) {
            return null;
        }
        for (ServiceInfo serviceInfo : services) {
            if (className.equals(serviceInfo.name)) {
                return serviceInfo;
            }
        }
        return null;
    }

    private static ActivityInfo findReceiver(ActivityInfo[] receivers, String className) {
        if (receivers == null) {
            return null;
        }
        for (ActivityInfo receiverInfo : receivers) {
            if (className.equals(receiverInfo.name)) {
                return receiverInfo;
            }
        }
        return null;
    }

    private static boolean hasRequestedPermission(PackageInfo packageInfo, String permissionName) {
        String[] requestedPermissions = packageInfo.requestedPermissions;
        if (requestedPermissions == null) {
            return false;
        }
        for (String requestedPermission : requestedPermissions) {
            if (permissionName.equals(requestedPermission)) {
                return true;
            }
        }
        return false;
    }
}
