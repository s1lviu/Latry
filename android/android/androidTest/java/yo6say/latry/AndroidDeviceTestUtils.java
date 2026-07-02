package yo6say.latry;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;

import androidx.test.platform.app.InstrumentationRegistry;

import org.json.JSONArray;
import org.json.JSONException;
import org.junit.Assert;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;

final class AndroidDeviceTestUtils {
    interface Condition {
        boolean isTrue() throws Exception;
    }

    private AndroidDeviceTestUtils() {
    }

    static Context targetContext() {
        return InstrumentationRegistry.getInstrumentation().getTargetContext().getApplicationContext();
    }

    static void runOnMainSync(Runnable runnable) {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(runnable);
    }

    static void grantStandardRuntimePermissions() throws Exception {
        grantRuntimePermission(Manifest.permission.RECORD_AUDIO);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            grantRuntimePermission(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            grantRuntimePermission(Manifest.permission.POST_NOTIFICATIONS);
        }
    }

    static void grantRuntimePermission(String permission) throws Exception {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        ParcelFileDescriptor descriptor = InstrumentationRegistry.getInstrumentation()
                .getUiAutomation()
                .executeShellCommand("pm grant " + targetContext().getPackageName() + " " + permission);
        if (descriptor != null) {
            descriptor.close();
        }
        SystemClock.sleep(200);
    }

    static void waitUntil(String description, long timeoutMs, Condition condition) throws Exception {
        long deadline = SystemClock.uptimeMillis() + timeoutMs;
        Throwable lastFailure = null;

        while (SystemClock.uptimeMillis() < deadline) {
            try {
                if (condition.isTrue()) {
                    return;
                }
                lastFailure = null;
            } catch (Throwable throwable) {
                lastFailure = throwable;
            }
            SystemClock.sleep(100);
        }

        if (lastFailure != null) {
            throw new AssertionError("Timed out waiting for " + description, lastFailure);
        }
        Assert.fail("Timed out waiting for " + description);
    }

    static void ensureServiceStopped() throws Exception {
        Context context = targetContext();
        context.stopService(new Intent(context, VoipBackgroundService.class));
        waitUntil("VoIP service to stop", 7000,
                () -> !isServicePresent(context.getPackageName() + "/.VoipBackgroundService"));
    }

    static List<String> parseRoutesJson(String routesJson) throws JSONException {
        List<String> routes = new ArrayList<>();
        JSONArray array = new JSONArray(routesJson);
        for (int i = 0; i < array.length(); ++i) {
            routes.add(array.optString(i));
        }
        return routes;
    }

    static void invokePrivateStatic(Class<?> type,
                                    String methodName,
                                    Class<?>[] parameterTypes,
                                    Object... args) throws Exception {
        Method method = type.getDeclaredMethod(methodName, parameterTypes);
        method.setAccessible(true);
        try {
            method.invoke(null, args);
        } catch (InvocationTargetException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof Exception) {
                throw (Exception) cause;
            }
            if (cause instanceof Error) {
                throw (Error) cause;
            }
            throw new AssertionError("Invocation failed for " + type.getName() + "::" + methodName, cause);
        }
    }

    static String runShellCommand(String command) throws Exception {
        ParcelFileDescriptor descriptor = InstrumentationRegistry.getInstrumentation()
                .getUiAutomation()
                .executeShellCommand(command);
        if (descriptor == null) {
            return "";
        }

        try (InputStream inputStream = new ParcelFileDescriptor.AutoCloseInputStream(descriptor);
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int read;
            while ((read = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, read);
            }
            return outputStream.toString("UTF-8");
        }
    }

    static boolean isServicePresent(String componentName) throws Exception {
        String output = activeServicesOutput();
        return !output.isEmpty() && output.contains(componentName);
    }

    static boolean isServiceCallStartCompleted(String componentName) throws Exception {
        String output = activeServicesOutput();
        if (!output.contains(componentName)) {
            return false;
        }
        return output.contains("callStart=true");
    }

    private static String activeServicesOutput() throws Exception {
        String output = runShellCommand("dumpsys activity services " + targetContext().getPackageName());
        String marker = "User 0 active services:";
        int markerIndex = output.indexOf(marker);
        if (markerIndex < 0) {
            return "";
        }
        return output.substring(markerIndex);
    }
}
