import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import QtQuick.Effects
import SvxlinkReflector.Client 1.0

Window {
    width: 360
    height: 640
    visible: true
    title: qsTr("Latry")
    
    // Dark mode support
    property bool isDarkMode: Application.styleHints.colorScheme === Qt.ColorScheme.Dark
    
    color: isDarkMode ? "#000000" : "#FFFFFF"
    
    // Android back button only — avoid re-entrant teardown on iOS
    onClosing: function (e) {
        if (Qt.platform.os === "android") {
            Qt.quit();
        } else {
            // iOS: let UIKit/Qt close the window naturally.
            // Don't force app quit; this avoids double teardown in QAccessible.
            e.accepted = true;
        }
    }
    
    onActiveChanged: {
        if (!active && ReflectorClient.pttActive) {
            ReflectorClient.pttReleased()
        }
    }
    
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive && ReflectorClient.pttActive) {
                ReflectorClient.pttReleased()
            }
        }
    }

    BatteryOptimizationDialog {
        id: batteryDialog
    }

    Connections {
        target: BatteryOptimizationHandler
        function onShowBatteryOptimizationInstructions(instructions) {
            batteryDialog.instructionsText = instructions
            batteryDialog.open()
        }
    }

    // Set application organization info to fix Settings warnings
    Component.onCompleted: {
        Qt.application.organization = "YO6SAY"
        Qt.application.organizationDomain = "yo6say.com"
        Qt.application.name = "Latry"
    }

    Settings {
        id: saved
        property string host: ""

        property string port: ""
        property string callsign: ""
        property string key: ""
        property string talkgroup: ""
        property string appWebsiteUrl: "https://latry.app/#ios"
    }

    // Store stable text values to avoid Android InputConnection issues
    property string stableHost: ""
    property string stablePort: ""
    property string stableKey: ""
    property string stableCallsign: ""
    property string stableTg: ""
    property bool allFieldsComplete: false

    function updateButtonState() {
        allFieldsComplete = stableHost !== "" &&
                           stablePort !== "" &&
                           stableKey !== "" &&
                           stableCallsign !== "" &&
                           stableTg !== ""
    }

    function updateStableValue(field, value) {
        if (value !== "") {
            if (field === "host") stableHost = value
            else if (field === "port") stablePort = value
            else if (field === "key") stableKey = value
            else if (field === "callsign") stableCallsign = value
            else if (field === "tg") stableTg = value
            updateButtonState()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors {
            // iOS-specific safe area handling
            topMargin: 12 + (Qt.platform.os === "ios" ? 44 : 0) // iOS status bar + safe area
            leftMargin: 12 + (Qt.platform.os === "ios" ? 0 : 0)
            rightMargin: 12 + (Qt.platform.os === "ios" ? 0 : 0)
            bottomMargin: 12 + (Qt.platform.os === "ios" ? 34 : 0) // iOS home indicator
        }
        spacing: Math.max(6, Math.min(10, height * 0.015))

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            
            Label {
                anchors.centerIn: parent
                text: qsTr("Latry")
                font.pixelSize: 24
                color: isDarkMode ? "#FFFFFF" : "#000000"
            }
            
            Button {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "ⓘ"
                font.pixelSize: 16
                width: 36
                height: 36
                
                // Hide info button on iOS
                visible: Qt.platform.os !== "ios"
                
                background: Rectangle {
                    color: "transparent"
                    radius: 18
                }
                
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: parent.font.pixelSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: "#666"
                }
                
                ToolTip.visible: hovered
                ToolTip.text: "Configure battery optimization for VoIP background service"
                onClicked: BatteryOptimizationHandler.requestBatteryOptimizationInstructions()
            }
        }

        Label {
            id: statusLabel
            Layout.fillWidth: true
            text: ReflectorClient.connectionStatus
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 16
            wrapMode: Text.WordWrap
            color: isDarkMode ? "#FFFFFF" : "#000000"
        }


        Frame {
            Layout.fillWidth: true
            
            background: Rectangle {
                color: isDarkMode ? "#1C1C1E" : "#F2F2F7"
                radius: Qt.platform.os === "ios" ? 10 : 4
                border.color: isDarkMode ? "#38383A" : "#C6C6C8"
                border.width: Qt.platform.os === "ios" ? 1 : 1
            }
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                TextField {
                    id: hostInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: Qt.platform.os === "ios" ? 50 : implicitHeight
                    leftPadding: Qt.platform.os === "ios" ? 16 : 6
                    rightPadding: Qt.platform.os === "ios" ? 16 : 6
                    placeholderText: qsTr("Host (e.g., reflector.145500.xyz)")
                    text: saved.host.length > 0 ? saved.host : ""
                    onTextChanged: updateStableValue("host", text)
                    Component.onCompleted: updateStableValue("host", text)
                    
                    color: isDarkMode ? "#FFFFFF" : "#000000"
                    placeholderTextColor: isDarkMode ? "#8E8E93" : "#8E8E93"
                    
                    background: Rectangle {
                        color: isDarkMode ? "#2C2C2E" : "#FFFFFF"
                        radius: Qt.platform.os === "ios" ? 8 : 4
                        border.color: isDarkMode ? "#38383A" : "#C6C6C8"
                        border.width: 1
                    }
                }

                TextField {
                    id: portInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: Qt.platform.os === "ios" ? 50 : implicitHeight
                    leftPadding: Qt.platform.os === "ios" ? 16 : 6
                    rightPadding: Qt.platform.os === "ios" ? 16 : 6
                    placeholderText: qsTr("Port (e.g., 5300)")
                    text: saved.port.length > 0 ? saved.port : "5300"
                    validator: IntValidator { bottom: 1; top: 65535 }
                    onTextChanged: updateStableValue("port", text)
                    Component.onCompleted: updateStableValue("port", text)
                    
                    color: isDarkMode ? "#FFFFFF" : "#000000"
                    placeholderTextColor: isDarkMode ? "#8E8E93" : "#8E8E93"
                    
                    background: Rectangle {
                        color: isDarkMode ? "#2C2C2E" : "#FFFFFF"
                        radius: Qt.platform.os === "ios" ? 8 : 4
                        border.color: isDarkMode ? "#38383A" : "#C6C6C8"
                        border.width: 1
                    }
                }

                TextField {
                    id: callsignInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: Qt.platform.os === "ios" ? 50 : implicitHeight
                    leftPadding: Qt.platform.os === "ios" ? 16 : 6
                    rightPadding: Qt.platform.os === "ios" ? 16 : 6
                    placeholderText: qsTr("Callsign (e.g., N0CALL)")
                    text: saved.callsign.length > 0 ? saved.callsign : "" // Default to empty
                    onTextChanged: updateStableValue("callsign", text)
                    Component.onCompleted: updateStableValue("callsign", text)
                    
                    color: isDarkMode ? "#FFFFFF" : "#000000"
                    placeholderTextColor: isDarkMode ? "#8E8E93" : "#8E8E93"
                    
                    background: Rectangle {
                        color: isDarkMode ? "#2C2C2E" : "#FFFFFF"
                        radius: Qt.platform.os === "ios" ? 8 : 4
                        border.color: isDarkMode ? "#38383A" : "#C6C6C8"
                        border.width: 1
                    }
                }

Item {
    Layout.fillWidth: true
    height: Qt.platform.os === "ios" ? 50 : keyInput.implicitHeight

    TextField {
        id: keyInput
        anchors.fill: parent
        placeholderText: qsTr("Authentication Key")
        echoMode: eye.checked ? TextInput.Normal : TextInput.Password
        leftPadding: Qt.platform.os === "ios" ? 16 : 6
        rightPadding: (Qt.platform.os === "ios" ? 16 : 6) + eye.width + 8
        text: saved.key.length ? saved.key : ""
        onTextChanged:  updateStableValue("key", text)
        Component.onCompleted: updateStableValue("key", text)
        
        color: isDarkMode ? "#FFFFFF" : "#000000"
        placeholderTextColor: isDarkMode ? "#8E8E93" : "#8E8E93"
        
        background: Rectangle {
            color: isDarkMode ? "#2C2C2E" : "#FFFFFF"
            radius: Qt.platform.os === "ios" ? 8 : 4
            border.color: isDarkMode ? "#38383A" : "#C6C6C8"
            border.width: 1
        }
    }

    /* eye is declared **after** the TextField and gets z-order 1 */
    Text {
        id: eye
        property bool checked: false
        z: 2                                      // ⇦ make sure it’s on top

        text: checked ? "🙈" : "👁️"
        font.pixelSize: keyInput.font.pixelSize
        anchors.verticalCenter: keyInput.verticalCenter
        anchors.right:        keyInput.right
        anchors.rightMargin:  6

        Accessible.role: Accessible.Button
        Accessible.name: checked ? qsTr("Hide key") : qsTr("Show key")

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor     // visual feedback
            onClicked: eye.checked = !eye.checked
            // stop click from bubbling back to the TextField
            propagateComposedEvents: false
        }
    }
}

                TextField {
                    id: tgInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: Qt.platform.os === "ios" ? 50 : implicitHeight
                    leftPadding: Qt.platform.os === "ios" ? 16 : 6
                    rightPadding: Qt.platform.os === "ios" ? 16 : 6
                    placeholderText: qsTr("Talkgroup (e.g., 9)")
                    text: saved.talkgroup.length > 0 ? saved.talkgroup : ""
                    validator: IntValidator {}
                    onTextChanged: updateStableValue("tg", text)
                    Component.onCompleted: updateStableValue("tg", text)
                    
                    color: isDarkMode ? "#FFFFFF" : "#000000"
                    placeholderTextColor: isDarkMode ? "#8E8E93" : "#8E8E93"
                    
                    background: Rectangle {
                        color: isDarkMode ? "#2C2C2E" : "#FFFFFF"
                        radius: Qt.platform.os === "ios" ? 8 : 4
                        border.color: isDarkMode ? "#38383A" : "#C6C6C8"
                        border.width: 1
                    }
                }
                
                Button {
                    id: connectButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: Qt.platform.os === "ios" ? 50 : implicitHeight
                    Layout.topMargin: 6
                    text: ReflectorClient.isDisconnected ? "Connect" : "Disconnect"
                    enabled: ReflectorClient.isDisconnected ? allFieldsComplete : true
                    
                    // iOS-specific styling with dark mode support
                    background: Rectangle {
                        color: {
                            if (connectButton.pressed) {
                                return Qt.platform.os === "ios" ? "#0066CC" : (isDarkMode ? "#4A4A4A" : "lightblue")
                            } else {
                                return Qt.platform.os === "ios" ? "#007AFF" : (isDarkMode ? "#5A5A5A" : "lightgray")
                            }
                        }
                        radius: Qt.platform.os === "ios" ? 8 : 4
                        border.color: Qt.platform.os === "ios" ? "transparent" : (isDarkMode ? "#666666" : "gray")
                        border.width: Qt.platform.os === "ios" ? 0 : 1
                    }
                    
                    contentItem: Text {
                        text: connectButton.text
                        color: Qt.platform.os === "ios" ? "white" : (isDarkMode ? "white" : "black")
                        font.bold: Qt.platform.os === "ios" ? true : false
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    Component.onCompleted: {
                        // Initialize stable values from saved settings
                        updateStableValue("host", hostInput.text)
                        updateStableValue("port", portInput.text)
                        updateStableValue("key", keyInput.text)
                        updateStableValue("callsign", callsignInput.text)
                        updateStableValue("tg", tgInput.text)
                        updateButtonState()
                    }

                    onClicked: {
                        if (ReflectorClient.isDisconnected) {
                            ReflectorClient.connectToServer(
                                stableHost.trim(),
                                parseInt(stablePort.trim()),
                                stableKey.trim(),
                                stableCallsign.trim(),
                                parseInt(stableTg.trim())
                            )
                        } else {
                            ReflectorClient.disconnectFromServer()
                        }
                    }
                }
            }
        }


        StackLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 30
            Layout.preferredHeight: 40
            currentIndex: ReflectorClient.pttActive ? 1 : 0

            Label {
                id: talkerLabel
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: ReflectorClient.currentTalker.length > 0 ?
                      "RX: " + ReflectorClient.currentTalker +
                      (ReflectorClient.currentTalkerName.length > 0 ?
                           " (" + ReflectorClient.currentTalkerName + ")" : "") :
                      ReflectorClient.isReceivingAudio ? "RX: Unknown" : ""
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                elide: Text.ElideRight
                color: isDarkMode ? "#FFFFFF" : "#000000"
            }

            Label {
                id: txTimerLabel
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: "TX Time: " + ReflectorClient.txTimeString
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                elide: Text.ElideRight
                color: isDarkMode ? "#FFFFFF" : "#000000"
            }
        }

        Item { 
            Layout.fillHeight: true
            Layout.minimumHeight: 10
        }

        Button {
            id: pttButton
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(60, Math.min(80, parent.height * 0.10))
            Layout.maximumHeight: 100
            text: ReflectorClient.pttActive ? "TRANSMITTING - TAP TO STOP" : "TAP TO TALK"
            // PTT enabled when connected, no one talking (known or unknown), and audio is ready
            enabled: ReflectorClient.connectionStatus.startsWith("Connected") &&
                     ReflectorClient.currentTalker.length === 0 &&
                     !ReflectorClient.isReceivingAudio &&
                     ReflectorClient.audioReady

            background: Rectangle {
                color: {
                    if (!pttButton.enabled) {
                        return Qt.platform.os === "ios" ? "#8E8E93" : "#CCCCCC" // iOS gray for disabled state
                    } else if (ReflectorClient.pttActive) {
                        return Qt.platform.os === "ios" ? "#FF3B30" : "darkred" // iOS red
                    } else if (pttButton.down) {
                        return Qt.platform.os === "ios" ? "#34C759" : "lightblue" // iOS green when pressed
                    } else {
                        return Qt.platform.os === "ios" ? "#4CD964" : "lightgray" // iOS green
                    }
                }
                radius: Qt.platform.os === "ios" ? 12 : 10
                border.color: {
                    if (ReflectorClient.pttActive) {
                        return Qt.platform.os === "ios" ? "transparent" : "red"
                    } else {
                        return Qt.platform.os === "ios" ? "transparent" : "gray"
                    }
                }
                border.width: Qt.platform.os === "ios" ? 0 : 2
                
                // iOS-style shadow effect
                layer.enabled: Qt.platform.os === "ios"
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#20000000"
                    shadowBlur: 0.6
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 2
                }
            }
            
            contentItem: Text {
                text: pttButton.text
                color: pttButton.enabled ? "white" : (Qt.platform.os === "ios" ? "#C7C7CC" : "#999999")
                font.bold: true
                font.pixelSize: Qt.platform.os === "ios" ? 18 : 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }

            onClicked: ReflectorClient.pttPressed()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 20
            Layout.preferredHeight: 24
            spacing: 4

            Text {
                text: qsTr("Made by Silviu YO6SAY")
                font.pixelSize: 10
                Layout.alignment: Qt.AlignLeft
                color: isDarkMode ? "#8E8E93" : "#666666"
            }

            Item { Layout.fillWidth: true }

            Text {
                text: 'v0.0.14'
                font.pixelSize: 10
                Layout.alignment: Qt.AlignCenter
                color: isDarkMode ? "#8E8E93" : "#666666"
            }

            Item { Layout.fillWidth: true }

            Text {
                textFormat: Text.RichText
                text: "<a href='https://latry.app/#ios'>About Latry</a>"
                font.pixelSize: 10
                color: isDarkMode ? "#0A84FF" : "blue"
                Layout.alignment: Qt.AlignRight
                onLinkActivated: Qt.openUrlExternally(link)
            }
        }

        Connections {
            target: ReflectorClient
            function onConnectionStatusChanged() {
                if (ReflectorClient.connectionStatus.startsWith("Connected")) {
                    saved.host = stableHost
                    saved.port = stablePort
                    saved.callsign = stableCallsign
                    saved.key = stableKey
                    saved.talkgroup = stableTg
                }
            }
            
        }
    }
}
