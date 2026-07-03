package yo6say.latry;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;

final class LatryAudioRoutePolicy {
    static final String ROUTE_SPEAKER = "speaker";
    static final String ROUTE_WIRED_HEADSET = "wired_headset";
    static final String ROUTE_BLUETOOTH = "bluetooth";
    static final String ROUTE_BLUETOOTH_PREFIX = "bluetooth:";

    static boolean isBluetoothRoute(String routeId) {
        if (routeId == null) return false;
        return ROUTE_BLUETOOTH.equals(routeId)
                || routeId.startsWith(ROUTE_BLUETOOTH_PREFIX);
    }

    private LatryAudioRoutePolicy() {
    }

    static String normalizeRouteId(String routeId) {
        if (routeId == null) {
            return ROUTE_SPEAKER;
        }

        String trimmed = routeId.trim();
        String normalized = trimmed.toLowerCase();
        if (ROUTE_WIRED_HEADSET.equals(normalized)) {
            return normalized;
        }
        if (isBluetoothRoute(normalized)) {
            // Preserve original casing for device name after "bluetooth:"
            if (normalized.startsWith(ROUTE_BLUETOOTH_PREFIX)) {
                return ROUTE_BLUETOOTH_PREFIX + trimmed.substring(ROUTE_BLUETOOTH_PREFIX.length());
            }
            return normalized;
        }
        return ROUTE_SPEAKER;
    }

    static List<String> orderedAvailableRoutes(Collection<String> routeIds) {
        List<String> orderedRoutes = new ArrayList<>();
        orderedRoutes.add(ROUTE_SPEAKER);

        boolean hasWired = false;
        List<String> bluetoothRoutes = new ArrayList<>();

        if (routeIds != null) {
            for (String routeId : routeIds) {
                if (routeId == null) continue;
                String normalized = normalizeRouteId(routeId);
                if (ROUTE_WIRED_HEADSET.equals(normalized)) {
                    hasWired = true;
                } else if (isBluetoothRoute(normalized)
                           && !bluetoothRoutes.contains(normalized)) {
                    bluetoothRoutes.add(normalized);
                }
            }
        }

        if (hasWired) {
            orderedRoutes.add(ROUTE_WIRED_HEADSET);
        }
        orderedRoutes.addAll(bluetoothRoutes);
        return orderedRoutes;
    }

    static String chooseTargetRoute(Collection<String> availableRoutes, String preferredRoute) {
        List<String> normalizedRoutes = orderedAvailableRoutes(availableRoutes);
        String normalizedPreferred = normalizeRouteId(preferredRoute);

        // Exact match first
        if (normalizedRoutes.contains(normalizedPreferred)) {
            return normalizedPreferred;
        }

        // If preferred was generic "bluetooth", pick first available BT device
        if (ROUTE_BLUETOOTH.equals(normalizedPreferred)) {
            for (String route : normalizedRoutes) {
                if (isBluetoothRoute(route)) return route;
            }
        }

        // Fallback: pick first available BT device
        for (String route : normalizedRoutes) {
            if (isBluetoothRoute(route)) return route;
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
