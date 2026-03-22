import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import SvxlinkReflector.Client 1.0
import QtMultimedia

Window {
    width: 360
    height: 640
    visible: true
    title: qsTr("Latry")
    
    // Handle Android back button to prevent white screen issue
    onClosing: (close) => {
        Qt.quit()  // Properly quit the application
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

    SoundEffect {
        id: totWarningSound
        source: "qrc:/sounds/tot_warning.wav"
    }

    // Set application organization info to fix Settings warnings
    Component.onCompleted: {
        Qt.application.organization = "YO6SAY"
        Qt.application.organizationDomain = "yo6say.com"
        Qt.application.name = "Latry"
        ReflectorClient.totEnabled = saved.totEnabled
        ReflectorClient.totDuration = saved.totDuration
        ReflectorClient.totWarnVisual = saved.totWarnVisual
        ReflectorClient.totWarnAudio = saved.totWarnAudio
        ReflectorClient.totWarnVibration = saved.totWarnVibration
    }

    Settings {
        id: saved
        property string host: ""

        property string port: ""
        property string callsign: ""
        property string key: ""
        property string talkgroup: ""
        property bool totEnabled: false
        property int totDuration: 180
        property bool totWarnVisual: true
        property bool totWarnAudio: true
        property bool totWarnVibration: true
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
            topMargin: 12 + parent.SafeArea.margins.top
            leftMargin: 12 + parent.SafeArea.margins.left
            rightMargin: 12 + parent.SafeArea.margins.right
            bottomMargin: 12 + parent.SafeArea.margins.bottom
        }
        spacing: Math.max(6, Math.min(10, height * 0.015))

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            
            Label {
                anchors.centerIn: parent
                text: qsTr("Latry")
                font.pixelSize: 24
            }
            
            Button {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "ⓘ"
                font.pixelSize: 16
                width: 36
                height: 36
                
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
        }


        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                TextField {
                    id: hostInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Host (e.g., reflector.145500.xyz)")
                    text: saved.host.length > 0 ? saved.host : ""
                    onTextChanged: updateStableValue("host", text)
                    Component.onCompleted: updateStableValue("host", text)
                }

                TextField {
                    id: portInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Port (e.g., 5300)")
                    text: saved.port.length > 0 ? saved.port : "5300"
                    validator: IntValidator { bottom: 1; top: 65535 }
                    onTextChanged: updateStableValue("port", text)
                    Component.onCompleted: updateStableValue("port", text)
                }

                TextField {
                    id: callsignInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Callsign (e.g., N0CALL)")
                    text: saved.callsign.length > 0 ? saved.callsign : "" // Default to empty
                    onTextChanged: updateStableValue("callsign", text)
                    Component.onCompleted: updateStableValue("callsign", text)
                }

Item {
    Layout.fillWidth: true
    height: keyInput.implicitHeight

    TextField {
        id: keyInput
        anchors.fill: parent
        placeholderText: qsTr("Authentication Key")
        echoMode: eye.checked ? TextInput.Normal : TextInput.Password
        rightPadding: eye.width + 8
        text: saved.key.length ? saved.key : ""
        onTextChanged:  updateStableValue("key", text)
        Component.onCompleted: updateStableValue("key", text)
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
                    placeholderText: qsTr("Talkgroup (e.g., 9)")
                    text: saved.talkgroup.length > 0 ? saved.talkgroup : ""
                    validator: IntValidator {}
                    onTextChanged: updateStableValue("tg", text)
                    Component.onCompleted: updateStableValue("tg", text)
                }
            }
        }

        Button {
            id: connectButton
            Layout.fillWidth: true
            text: ReflectorClient.isDisconnected ? "Connect" : "Disconnect"
            enabled: ReflectorClient.isDisconnected ? allFieldsComplete : true

            Component.onCompleted: {
                // Initialize stable values from saved settings
                updateStableValue("host", hostInput.text)
                updateStableValue("port", portInput.text)
                updateStableValue("key", keyInput.text)
                updateStableValue("callsign", callsignInput.text)
                updateStableValue("tg", tgInput.text)
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

        Frame {
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: "Transmission Settings"
                    font.pixelSize: 14
                    font.bold: true
                    color: "#6C6C70"
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Time Out Timer"
                        font.pixelSize: 16
                        Layout.fillWidth: true
                    }
                    Switch {
                        id: totEnabledSwitch
                        checked: saved.totEnabled
                        onCheckedChanged: {
                            saved.totEnabled = checked
                            ReflectorClient.totEnabled = checked
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: totEnabledSwitch.checked
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Duration"
                            font.pixelSize: 14
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            id: totDurationCombo
                            model: ["0:30", "1:00", "1:30", "2:00", "2:30", "3:00", "Custom"]
                            property var durations: [30, 60, 90, 120, 150, 180]
                            property bool initialized: false

                            Component.onCompleted: {
                                var idx = durations.indexOf(saved.totDuration)
                                currentIndex = idx >= 0 ? idx : 6
                                if (idx < 0) customDurationInput.text = saved.totDuration.toString()
                                initialized = true
                            }

                            onCurrentIndexChanged: {
                                if (!initialized) return
                                if (currentIndex >= 0 && currentIndex < durations.length) {
                                    saved.totDuration = durations[currentIndex]
                                    ReflectorClient.totDuration = durations[currentIndex]
                                }
                            }
                        }
                    }

                    TextField {
                        id: customDurationInput
                        Layout.fillWidth: true
                        visible: totDurationCombo.currentIndex === 6
                        placeholderText: "Seconds (10-600)"
                        text: saved.totDuration.toString()
                        validator: IntValidator { bottom: 10; top: 600 }
                        onEditingFinished: {
                            var val = parseInt(text)
                            if (val >= 10 && val <= 600) {
                                saved.totDuration = val
                                ReflectorClient.totDuration = val
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Visual warning"; font.pixelSize: 14; Layout.fillWidth: true }
                        Switch {
                            checked: saved.totWarnVisual
                            onCheckedChanged: { saved.totWarnVisual = checked; ReflectorClient.totWarnVisual = checked }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Audio warning"; font.pixelSize: 14; Layout.fillWidth: true }
                        Switch {
                            checked: saved.totWarnAudio
                            onCheckedChanged: { saved.totWarnAudio = checked; ReflectorClient.totWarnAudio = checked }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Vibration"; font.pixelSize: 14; Layout.fillWidth: true }
                        Switch {
                            checked: saved.totWarnVibration
                            onCheckedChanged: { saved.totWarnVibration = checked; ReflectorClient.totWarnVibration = checked }
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true

            Label {
                id: talkerLabel
                anchors.fill: parent
                text: ReflectorClient.currentTalker.length > 0 ?
                      "RX: " + ReflectorClient.currentTalker +
                      (ReflectorClient.currentTalkerName.length > 0 ?
                           " (" + ReflectorClient.currentTalkerName + ")" : "") :
                      ReflectorClient.isReceivingAudio ? "RX: Unknown" : ""
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                visible: !ReflectorClient.pttActive && (ReflectorClient.currentTalker.length > 0 || ReflectorClient.isReceivingAudio)
                elide: Text.ElideRight
            }

            Label {
                id: txTimerLabel
                anchors.fill: parent
                text: "TX Time: " + ReflectorClient.txTimeString
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                visible: ReflectorClient.pttActive
                elide: Text.ElideRight
                color: ReflectorClient.totWarningActive ? "#FF9500" : "black"
            }
        }

        Label {
            id: totCutoffLabel
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            text: "TOT reached"
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 18
            font.bold: true
            color: "#FF3B30"
            visible: false

            Timer {
                id: totCutoffDismissTimer
                interval: 2000
                onTriggered: totCutoffLabel.visible = false
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
                    if (ReflectorClient.pttActive) {
                        return ReflectorClient.totWarningActive ? "#FF9500" : "darkred"
                    }
                    return pttButton.down ? "lightblue" : "lightgray"
                }
                radius: 10
                border.color: ReflectorClient.pttActive ? "red" : "gray"
                border.width: 2
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
            }

            Item { Layout.fillWidth: true }

            Text {
                text: 'v0.0.14'
                font.pixelSize: 10
                Layout.alignment: Qt.AlignCenter
            }

            Item { Layout.fillWidth: true }

            Text {
                textFormat: Text.RichText
                text: "<a href='https://buymeacoffee.com/silviu'>Buy me a coffee</a>"
                font.pixelSize: 10
                color: "blue"
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

            function onTotWarningTriggered() {
                if (ReflectorClient.totWarnAudio) {
                    totWarningSound.play()
                }
                if (ReflectorClient.totWarnVibration) {
                    ReflectorClient.vibrateDevice()
                }
            }

            function onTotCutoffTriggered() {
                totCutoffLabel.visible = true
                totCutoffDismissTimer.start()
            }
        }
    }
}
