package yo6say.latry;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public final class LatryAudioRoutePolicySelfTest {
    private LatryAudioRoutePolicySelfTest() {
    }

    public static void main(String[] args) {
        testNormalizeRouteId();
        testOrderedAvailableRoutes();
        testChooseTargetRoute();
        testRoutesJson();
        System.out.println("LatryAudioRoutePolicySelfTest passed");
    }

    private static void testNormalizeRouteId() {
        assertEquals("speaker", LatryAudioRoutePolicy.normalizeRouteId(null), "null route should default to speaker");
        assertEquals("speaker", LatryAudioRoutePolicy.normalizeRouteId(""), "empty route should default to speaker");
        assertEquals("speaker", LatryAudioRoutePolicy.normalizeRouteId("car"), "unknown route should default to speaker");
        assertEquals("wired_headset", LatryAudioRoutePolicy.normalizeRouteId(" wired_headset "), "wired route should be normalized");
        assertEquals("bluetooth", LatryAudioRoutePolicy.normalizeRouteId("BLUETOOTH"), "bluetooth route should be normalized");
    }

    private static void testOrderedAvailableRoutes() {
        List<String> ordered = LatryAudioRoutePolicy.orderedAvailableRoutes(
            Arrays.asList("bluetooth", "wired_headset", "speaker", "bluetooth", "car"));
        assertListEquals(Arrays.asList("speaker", "wired_headset", "bluetooth"), ordered,
            "available routes should be normalized, deduplicated, and ordered");

        List<String> speakerOnly = LatryAudioRoutePolicy.orderedAvailableRoutes(Collections.<String>emptyList());
        assertListEquals(Collections.singletonList("speaker"), speakerOnly,
            "speaker should always be present");
    }

    private static void testChooseTargetRoute() {
        List<String> availableRoutes = Arrays.asList("speaker", "wired_headset", "bluetooth");
        assertEquals("wired_headset",
            LatryAudioRoutePolicy.chooseTargetRoute(availableRoutes, " wired_headset "),
            "preferred route should win when available");
        assertEquals("bluetooth",
            LatryAudioRoutePolicy.chooseTargetRoute(Arrays.asList("speaker", "bluetooth"), "wired_headset"),
            "bluetooth should be the first fallback when preferred route is unavailable");
        assertEquals("speaker",
            LatryAudioRoutePolicy.chooseTargetRoute(Collections.singletonList("speaker"), "bluetooth"),
            "speaker should be the last fallback");
    }

    private static void testRoutesJson() {
        assertEquals("[\"speaker\",\"wired_headset\",\"bluetooth\"]",
            LatryAudioRoutePolicy.routesJson(Arrays.asList("bluetooth", "wired_headset", "speaker")),
            "routes JSON should preserve normalized route order");
        assertEquals("[\"speaker\"]",
            LatryAudioRoutePolicy.routesJson(Collections.singletonList("speaker")),
            "routes JSON should include speaker");
    }

    private static void assertEquals(String expected, String actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }

    private static void assertListEquals(List<String> expected, List<String> actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + " expected=" + expected + " actual=" + actual);
        }
    }
}
