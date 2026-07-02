package yo6say.latry;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.RouteInfo;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.net.InetAddress;
import java.util.List;
import java.util.Objects;

final class NetworkHandoverMonitor {
    static final int REASON_INITIAL = 1;
    static final int REASON_AVAILABLE = 2;
    static final int REASON_CAPABILITIES_CHANGED = 3;
    static final int REASON_LINK_PROPERTIES_CHANGED = 4;
    static final int REASON_LOST = 5;

    static final int TRANSPORT_UNKNOWN = 0;
    static final int TRANSPORT_WIFI = 1;
    static final int TRANSPORT_CELLULAR = 2;
    static final int TRANSPORT_ETHERNET = 3;
    static final int TRANSPORT_OTHER = 4;

    private static final String TAG = "NetworkHandoverMonitor";
    private static final long CALLBACK_DEBOUNCE_MS = 350L;

    interface Listener {
        void onNetworkSnapshotChanged(NetworkSnapshot snapshot);
    }

    static final class NetworkSnapshot {
        final int generation;
        final int reason;
        final boolean hasDefaultNetwork;
        final boolean validated;
        final int transport;
        final boolean metered;
        final boolean captivePortal;
        final boolean routeChanged;
        final long networkHandle;
        final String linkFingerprint;

        NetworkSnapshot(int generation,
                        int reason,
                        boolean hasDefaultNetwork,
                        boolean validated,
                        int transport,
                        boolean metered,
                        boolean captivePortal,
                        boolean routeChanged,
                        long networkHandle,
                        String linkFingerprint) {
            this.generation = generation;
            this.reason = reason;
            this.hasDefaultNetwork = hasDefaultNetwork;
            this.validated = validated;
            this.transport = transport;
            this.metered = metered;
            this.captivePortal = captivePortal;
            this.routeChanged = routeChanged;
            this.networkHandle = networkHandle;
            this.linkFingerprint = linkFingerprint != null ? linkFingerprint : "";
        }

        @Override
        public String toString() {
            return "NetworkSnapshot{"
                    + "generation=" + generation
                    + ", reason=" + reason
                    + ", hasDefaultNetwork=" + hasDefaultNetwork
                    + ", validated=" + validated
                    + ", transport=" + transport
                    + ", metered=" + metered
                    + ", captivePortal=" + captivePortal
                    + ", routeChanged=" + routeChanged
                    + ", networkHandle=" + networkHandle
                    + '}';
        }
    }

    private final ConnectivityManager connectivityManager;
    private final Handler mainHandler;
    private final Listener listener;
    private final ConnectivityManager.NetworkCallback networkCallback =
            new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    scheduleCurrentSnapshot(REASON_AVAILABLE);
                }

                @Override
                public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {
                    scheduleCurrentSnapshot(REASON_CAPABILITIES_CHANGED);
                }

                @Override
                public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {
                    scheduleCurrentSnapshot(REASON_LINK_PROPERTIES_CHANGED);
                }

                @Override
                public void onLost(Network network) {
                    scheduleCurrentSnapshot(REASON_LOST);
                }
            };

    private final Runnable emitRunnable = new Runnable() {
        @Override
        public void run() {
            emitPendingSnapshot();
        }
    };

    private boolean registered = false;
    private int generation = 0;
    private NetworkSnapshot lastDeliveredSnapshot = null;
    private NetworkSnapshot pendingSnapshot = null;

    NetworkHandoverMonitor(Context context, Listener listener) {
        Context appContext = context != null ? context.getApplicationContext() : null;
        this.connectivityManager = appContext != null
                ? (ConnectivityManager) appContext.getSystemService(Context.CONNECTIVITY_SERVICE)
                : null;
        this.mainHandler = new Handler(Looper.getMainLooper());
        this.listener = listener;
    }

    boolean start() {
        if (registered) {
            scheduleCurrentSnapshot(REASON_INITIAL);
            return true;
        }

        if (connectivityManager == null) {
            Log.w(TAG, "ConnectivityManager is unavailable; default-network monitoring disabled");
            return false;
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            Log.i(TAG, "Default network callback is unavailable on this API level; keeping legacy polling only");
            return false;
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                connectivityManager.registerDefaultNetworkCallback(networkCallback, mainHandler);
            } else {
                connectivityManager.registerDefaultNetworkCallback(networkCallback);
            }
            registered = true;
            scheduleCurrentSnapshot(REASON_INITIAL);
            Log.d(TAG, "Default network callback registered");
            return true;
        } catch (RuntimeException exception) {
            Log.w(TAG, "Failed to register default network callback", exception);
            registered = false;
            return false;
        }
    }

    void stop() {
        mainHandler.removeCallbacks(emitRunnable);
        pendingSnapshot = null;

        if (!registered || connectivityManager == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
            registered = false;
            return;
        }

        try {
            connectivityManager.unregisterNetworkCallback(networkCallback);
        } catch (IllegalArgumentException exception) {
            Log.w(TAG, "Default network callback was already unregistered", exception);
        } catch (RuntimeException exception) {
            Log.w(TAG, "Failed to unregister default network callback cleanly", exception);
        } finally {
            registered = false;
        }
    }

    boolean isRegistered() {
        return registered;
    }

    void dispatchSnapshotForTesting(NetworkSnapshot snapshot) {
        pendingSnapshot = snapshot;
        emitPendingSnapshot();
    }

    private void scheduleCurrentSnapshot(int reason) {
        NetworkSnapshot snapshot = buildSnapshot(reason);
        if (!isMeaningfulChange(snapshot, pendingSnapshot != null ? pendingSnapshot : lastDeliveredSnapshot)) {
            return;
        }

        pendingSnapshot = snapshot;
        mainHandler.removeCallbacks(emitRunnable);
        long delayMs = reason == REASON_INITIAL ? 0L : CALLBACK_DEBOUNCE_MS;
        mainHandler.postDelayed(emitRunnable, delayMs);
    }

    private void emitPendingSnapshot() {
        NetworkSnapshot snapshot = pendingSnapshot;
        pendingSnapshot = null;
        if (snapshot == null) {
            return;
        }

        if (!isMeaningfulChange(snapshot, lastDeliveredSnapshot)) {
            return;
        }

        lastDeliveredSnapshot = snapshot;
        if (listener != null) {
            listener.onNetworkSnapshotChanged(snapshot);
        }
    }

    private NetworkSnapshot buildSnapshot(int reason) {
        Network activeNetwork = connectivityManager != null ? connectivityManager.getActiveNetwork() : null;
        boolean hasDefaultNetwork = activeNetwork != null;
        NetworkCapabilities capabilities = hasDefaultNetwork
                ? connectivityManager.getNetworkCapabilities(activeNetwork)
                : null;
        LinkProperties linkProperties = hasDefaultNetwork
                ? connectivityManager.getLinkProperties(activeNetwork)
                : null;

        boolean validated = capabilities != null
                && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
        boolean captivePortal = capabilities != null
                && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_CAPTIVE_PORTAL);
        int transport = classifyTransport(capabilities);
        boolean metered = connectivityManager != null && connectivityManager.isActiveNetworkMetered();
        long networkHandle = activeNetwork != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                ? activeNetwork.getNetworkHandle()
                : -1L;
        String linkFingerprint = buildLinkFingerprint(linkProperties);

        boolean routeChanged = false;
        if (lastDeliveredSnapshot != null && reason != REASON_INITIAL) {
            routeChanged = networkHandle != lastDeliveredSnapshot.networkHandle
                    || !Objects.equals(linkFingerprint, lastDeliveredSnapshot.linkFingerprint)
                    || hasDefaultNetwork != lastDeliveredSnapshot.hasDefaultNetwork;
        }

        int nextGeneration = generation;
        if (nextGeneration == 0) {
            nextGeneration = 1;
        } else if (routeChanged || hasDefaultNetwork != (lastDeliveredSnapshot != null
                && lastDeliveredSnapshot.hasDefaultNetwork)) {
            nextGeneration += 1;
        }
        generation = nextGeneration;

        return new NetworkSnapshot(
                nextGeneration,
                reason,
                hasDefaultNetwork,
                validated,
                transport,
                metered,
                captivePortal,
                routeChanged,
                networkHandle,
                linkFingerprint);
    }

    private static boolean isMeaningfulChange(NetworkSnapshot snapshot, NetworkSnapshot baseline) {
        if (snapshot == null) {
            return false;
        }
        if (baseline == null) {
            return true;
        }

        return snapshot.generation != baseline.generation
                || snapshot.hasDefaultNetwork != baseline.hasDefaultNetwork
                || snapshot.validated != baseline.validated
                || snapshot.transport != baseline.transport
                || snapshot.metered != baseline.metered
                || snapshot.captivePortal != baseline.captivePortal
                || snapshot.routeChanged != baseline.routeChanged;
    }

    private static int classifyTransport(NetworkCapabilities capabilities) {
        if (capabilities == null) {
            return TRANSPORT_UNKNOWN;
        }
        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            return TRANSPORT_WIFI;
        }
        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
            return TRANSPORT_CELLULAR;
        }
        if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
            return TRANSPORT_ETHERNET;
        }
        return TRANSPORT_OTHER;
    }

    private static String buildLinkFingerprint(LinkProperties linkProperties) {
        if (linkProperties == null) {
            return "";
        }

        StringBuilder builder = new StringBuilder();
        builder.append("if=").append(nullSafe(linkProperties.getInterfaceName()));

        for (LinkAddress address : linkProperties.getLinkAddresses()) {
            builder.append("|addr=").append(address.toString());
        }
        for (RouteInfo route : linkProperties.getRoutes()) {
            builder.append("|route=").append(route.toString());
        }
        List<InetAddress> dnsServers = linkProperties.getDnsServers();
        for (InetAddress address : dnsServers) {
            builder.append("|dns=").append(address != null ? address.getHostAddress() : "");
        }

        return builder.toString();
    }

    private static String nullSafe(String value) {
        return value != null ? value : "";
    }
}
