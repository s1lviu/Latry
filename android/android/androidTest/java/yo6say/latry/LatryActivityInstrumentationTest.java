package yo6say.latry;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.lang.reflect.Field;

@RunWith(AndroidJUnit4.class)
public final class LatryActivityInstrumentationTest {
    @Before
    public void setUp() throws Exception {
        AndroidIntegrationTestHooks.setEnabled(true);
        setCurrentFocusModeForTesting(0);
    }

    @After
    public void tearDown() throws Exception {
        setCurrentFocusModeForTesting(0);
        AndroidIntegrationTestHooks.setEnabled(false);
    }

    @Test
    public void genericFocusRequestTargetsCurrentTransmitMode() throws Exception {
        setCurrentFocusModeForTesting(2);
        assertEquals("TX_EXCLUSIVE", LatryActivity.requestedFocusModeNameForTesting());

        setCurrentFocusModeForTesting(0);
        assertEquals("RX_MIXING", LatryActivity.requestedFocusModeNameForTesting());
    }

    @Test
    public void optionalPermissionResultsStayOutOfQtDispatcher() {
        assertFalse(LatryActivity.shouldForwardPermissionResultToQt(1001));
        assertTrue(LatryActivity.shouldForwardPermissionResultToQt(1002));
    }

    @Test
    public void nativeShutdownPreparationRequiresFinishingActivityWithoutBackgroundService() {
        assertFalse(LatryActivity.shouldPrepareNativeShutdown(false, false, false));
        assertFalse(LatryActivity.shouldPrepareNativeShutdown(true, true, false));
        assertFalse(LatryActivity.shouldPrepareNativeShutdown(true, false, true));
        assertTrue(LatryActivity.shouldPrepareNativeShutdown(true, false, false));
        assertFalse(LatryActivity.shouldPrepareNativeShutdown(true, false, false, true));
    }

    @Test
    public void qtAccessibilityWorkaroundTargetsHuaweiFamilyDevices() {
        assertTrue(LatryActivity.shouldDisableQtAndroidAccessibility("Huawei", ""));
        assertTrue(LatryActivity.shouldDisableQtAndroidAccessibility("HUAWEI", ""));
        assertTrue(LatryActivity.shouldDisableQtAndroidAccessibility("Honor", ""));
        assertTrue(LatryActivity.shouldDisableQtAndroidAccessibility("", "HONOR"));
        assertFalse(LatryActivity.shouldDisableQtAndroidAccessibility("Samsung", "Galaxy"));
    }

    private static void setCurrentFocusModeForTesting(int mode) throws Exception {
        Field field = LatryActivity.class.getDeclaredField("currentFocusMode");
        field.setAccessible(true);
        field.setInt(null, mode);
    }
}
