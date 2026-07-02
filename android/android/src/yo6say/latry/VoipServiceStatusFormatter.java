package yo6say.latry;

public final class VoipServiceStatusFormatter {
    private VoipServiceStatusFormatter() {
    }

    public static String buildConnectingStatus(String host, int port) {
        if (host == null || host.isEmpty() || port <= 0) {
            return "Connecting...";
        }
        return "Connecting to " + host + ":" + port;
    }

    public static String buildDisconnectedStatus(String serverHost, int serverPort) {
        if (serverHost != null && !serverHost.isEmpty() && serverPort > 0) {
            return "Ready to connect to " + serverHost + ":" + serverPort;
        }
        return "Android Auto ready";
    }

    public static String buildNotificationTitle(boolean connected, boolean receiving, boolean transmitting) {
        if (transmitting) {
            return "Latry TX Active";
        }
        if (receiving) {
            return "Latry RX Active";
        }
        if (connected) {
            return "Latry Monitoring";
        }
        return "Latry Ready";
    }

    public static String buildNotificationText(boolean connected,
                                               boolean receiving,
                                               boolean transmitting,
                                               int talkgroup,
                                               String currentTalker,
                                               String connectionStatus,
                                               String serverHost,
                                               int serverPort) {
        if (transmitting) {
            if (talkgroup > 0) {
                return "Transmitting on TG " + talkgroup;
            }
            return "Transmitting";
        }
        if (receiving) {
            if (currentTalker != null && !currentTalker.isEmpty()) {
                return "Receiving " + currentTalker;
            }
            return "Receiving audio";
        }
        if (connected) {
            if (talkgroup > 0) {
                return "Monitoring TG " + talkgroup;
            }
            return "Connected";
        }

        String normalizedConnectionStatus = connectionStatus == null ? "" : connectionStatus;
        if (!normalizedConnectionStatus.isEmpty()) {
            return normalizedConnectionStatus;
        }
        return buildDisconnectedStatus(serverHost, serverPort);
    }

    public static String buildDetailedNotificationText(String connectionStatus,
                                                       String callsign,
                                                       int talkgroup,
                                                       boolean receiving,
                                                       boolean transmitting,
                                                       String currentTalker) {
        StringBuilder details = new StringBuilder();
        details.append(connectionStatus == null ? "" : connectionStatus);
        if (callsign != null && !callsign.isEmpty()) {
            details.append(" | ").append(callsign);
        }
        if (talkgroup > 0) {
            details.append(" | TG ").append(talkgroup);
        }
        if (transmitting) {
            details.append(" | TX");
        } else if (receiving) {
            details.append(" | RX");
            if (currentTalker != null && !currentTalker.isEmpty()) {
                details.append(" ").append(currentTalker);
            }
        }
        return details.toString();
    }
}
