package yo6say.latry;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;

final class LatryAudioRoutePolicy {
    static final String ROUTE_SPEAKER = "speaker";
    static final String ROUTE_WIRED_HEADSET = "wired_headset";
    static final String ROUTE_BLUETOOTH = "bluetooth";

    private LatryAudioRoutePolicy() {
    }

    static String normalizeRouteId(String routeId) {
        if (routeId == null) {
            return ROUTE_SPEAKER;
        }

        String normalized = routeId.trim().toLowerCase();
        if (ROUTE_WIRED_HEADSET.equals(normalized) || ROUTE_BLUETOOTH.equals(normalized)) {
            return normalized;
        }
        return ROUTE_SPEAKER;
    }

    static List<String> orderedAvailableRoutes(Collection<String> routeIds) {
        LinkedHashSet<String> normalizedRoutes = new LinkedHashSet<>();
        if (routeIds != null) {
            for (String routeId : routeIds) {
                normalizedRoutes.add(normalizeRouteId(routeId));
            }
        }

        normalizedRoutes.add(ROUTE_SPEAKER);

        List<String> orderedRoutes = new ArrayList<>();
        orderedRoutes.add(ROUTE_SPEAKER);
        if (normalizedRoutes.contains(ROUTE_WIRED_HEADSET)) {
            orderedRoutes.add(ROUTE_WIRED_HEADSET);
        }
        if (normalizedRoutes.contains(ROUTE_BLUETOOTH)) {
            orderedRoutes.add(ROUTE_BLUETOOTH);
        }
        return orderedRoutes;
    }

    static String chooseTargetRoute(Collection<String> availableRoutes, String preferredRoute) {
        List<String> normalizedRoutes = orderedAvailableRoutes(availableRoutes);
        String normalizedPreferredRoute = normalizeRouteId(preferredRoute);
        if (normalizedRoutes.contains(normalizedPreferredRoute)) {
            return normalizedPreferredRoute;
        }
        if (normalizedRoutes.contains(ROUTE_BLUETOOTH)) {
            return ROUTE_BLUETOOTH;
        }
        if (normalizedRoutes.contains(ROUTE_WIRED_HEADSET)) {
            return ROUTE_WIRED_HEADSET;
        }
        return ROUTE_SPEAKER;
    }

    static String routesJson(Collection<String> routeIds) {
        List<String> orderedRoutes = orderedAvailableRoutes(routeIds);
        StringBuilder builder = new StringBuilder("[");
        for (int index = 0; index < orderedRoutes.size(); ++index) {
            if (index > 0) {
                builder.append(',');
            }
            builder.append('"').append(orderedRoutes.get(index)).append('"');
        }
        builder.append(']');
        return builder.toString();
    }
}