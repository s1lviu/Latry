pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SvxlinkReflector.Client 1.0

Page {
    id: page

    required property real contentPadding
    required property real safeAreaTop
    required property real safeAreaLeft
    required property real safeAreaRight
    required property real safeAreaBottom
    required property var uiMetrics
    required property color surfaceColor
    required property color tintedSurfaceColor
    required property color borderColor
    required property color accentColor
    required property var profilesModel
    required property var nodeInfoReadOnlyEntries
    required property var nodeInfoReservedKeys
    required property var nodeInfoPropertiesModel
    required property string selectedProfileId
    required property bool profileSwitchInProgress
    required property var availableAudioRoutes
    required property string currentAudioRoute
    required property string preferredAudioRoute
    required property real rxAudioLevelDb
    required property real txAudioLevelDb
    required property int txTimeoutSeconds
    required property int pttHangTimeMs
    required property bool tapToTalkButtonVisible
    required property var reflectorClient
    property bool downloadableLanguagesExpanded: false
    property string compactSection: ""
    readonly property bool compactSettingsMode: page.uiMetrics.isSinglePaneScreen
    readonly property bool compactSectionChooserVisible: compactSettingsMode && compactSection === ""
    readonly property bool stackedHardwarePttControls: page.uiMetrics.isSinglePaneScreen
    readonly property bool liveTranscriptionSettingsVisible: page.uiMetrics.liveTranscriptionAllowed
                                                            && Qt.platform.os === "android"
                                                            && (page.reflectorClient.transcriptionInstalledLanguages.length > 0
                                                                || page.reflectorClient.transcriptionDownloadableLanguages.length > 0
                                                                || page.reflectorClient.transcriptionModelDownloadAvailable
                                                                || page.reflectorClient.transcriptionModelDownloadInProgress)

    signal backRequested()
    signal addProfileRequested()
    signal selectProfileRequested(string profileId)
    signal editProfileRequested(int editIndex, var draftProfile)
    signal deleteProfileRequested(int editIndex)
    signal audioRouteRequested(string routeId)
    signal rxAudioLevelRequested(real levelDb)
    signal txAudioLevelRequested(real levelDb)
    signal txTimeoutSecondsRequested(int seconds)
    signal pttHangTimeMsRequested(int milliseconds)
    signal tapToTalkButtonVisibleRequested(bool visible)
    signal hardwarePttEnabledRequested(bool enabled)
    signal learnedHardwarePttKeyCodeRequested(int keyCode)
    signal clearLearnedHardwarePttKeyCodeRequested()
    signal liveTranscriptionEnabledRequested(bool enabled)
    signal transcriptionLanguageDownloadRequested(string languageTag)
    signal transcriptionLanguageSettingsRequested()
    signal addNodeInfoPropertyRequested()
    signal updateNodeInfoPropertyRequested(int index, string key, string value)
    signal deleteNodeInfoPropertyRequested(int index)
    signal batteryOptimizationRequested()

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Settings")

    function compactSectionTitle(sectionId) {
        if (sectionId === "profiles")
            return qsTr("Profiles")
        if (sectionId === "audio")
            return qsTr("Audio")
        if (sectionId === "radio")
            return qsTr("Radio")
        if (sectionId === "device")
            return qsTr("Device")
        if (sectionId === "nodeinfo")
            return qsTr("Node Info")
        return qsTr("Settings")
    }

    function audioRouteName(routeId) {
        for (let i = 0; i < page.availableAudioRoutes.length; ++i) {
            const route = page.availableAudioRoutes[i]
            if (route.id === routeId)
                return route.name
        }

        if (routeId === "wired_headset")
            return qsTr("Wired Headset")
        if (routeId === "bluetooth")
            return qsTr("Bluetooth")
        return qsTr("Speaker")
    }

    function audioRouteAvailable(routeId) {
        for (let i = 0; i < page.availableAudioRoutes.length; ++i) {
            if (page.availableAudioRoutes[i].id === routeId)
                return true
        }
        return false
    }

    function audioRouteDescription(routeId) {
        if (routeId === "wired_headset")
            return qsTr("Wired audio route")
        if (routeId === "bluetooth" || routeId.startsWith("bluetooth:"))
            return qsTr("Bluetooth voice route")
        return qsTr("Speaker + Internal mic")
    }

    function isZeroLevel(levelDb) {
        return Math.abs(Number(levelDb)) < 0.05
    }

    function signedDbText(levelDb, decimals) {
        const numericLevel = Number(levelDb)
        const fixedLevel = numericLevel.toFixed(decimals)
        return (numericLevel > 0 ? "+" : "") + fixedLevel + " dB"
    }

    function rxAudioLevelText(levelDb) {
        if (page.isZeroLevel(levelDb))
            return qsTr("Off")
        return page.signedDbText(levelDb, 1)
    }

    function txAudioLevelText(levelDb) {
        if (page.isZeroLevel(levelDb))
            return qsTr("Normal")
        return page.signedDbText(levelDb, 0)
    }

    function hasCustomAudioLevels() {
        return !page.isZeroLevel(page.rxAudioLevelDb) || !page.isZeroLevel(page.txAudioLevelDb)
    }

    function learnedHardwarePttKeyCodeText() {
        return page.reflectorClient.learnedHardwarePttKeyCode > 0
                ? String(page.reflectorClient.learnedHardwarePttKeyCode)
                : ""
    }

    function isReservedNodeInfoKey(key) {
        const normalizedKey = String(key).trim().toLowerCase()
        if (normalizedKey === "")
            return false

        for (let i = 0; i < page.nodeInfoReservedKeys.length; ++i) {
            if (String(page.nodeInfoReservedKeys[i]).toLowerCase() === normalizedKey)
                return true
        }

        return false
    }

    onCompactSettingsModeChanged: {
        if (!compactSettingsMode)
            compactSection = ""
    }

    onRxAudioLevelDbChanged: {
        if (!rxLevelSlider.pressed)
            rxLevelSlider.value = page.rxAudioLevelDb
    }

    onTxAudioLevelDbChanged: {
        if (!txLevelSlider.pressed)
            txLevelSlider.value = page.txAudioLevelDb
    }

    onTxTimeoutSecondsChanged: {
        if (!txTimeoutField.activeFocus)
            txTimeoutField.text = String(page.txTimeoutSeconds)
    }

    onPttHangTimeMsChanged: {
        if (!pttHangTimeField.activeFocus)
            pttHangTimeField.text = String(page.pttHangTimeMs)
    }

    Component.onCompleted: {
        rxLevelSlider.value = page.rxAudioLevelDb
        txLevelSlider.value = page.txAudioLevelDb
        hardwarePttKeyCodeField.text = page.learnedHardwarePttKeyCodeText()
    }

    Connections {
        target: page.reflectorClient

        function onHardwarePttSettingsChanged() {
            if (!hardwarePttKeyCodeField.activeFocus)
                hardwarePttKeyCodeField.text = page.learnedHardwarePttKeyCodeText()
        }

        function onHardwarePttLearningActiveChanged() {
            if (!page.reflectorClient.hardwarePttLearningActive && pttLearningDialog.opened)
                pttLearningDialog.close()
        }

        function onHardwarePttLearningResultChanged() {
            const result = page.reflectorClient.hardwarePttLearningResult
            if (result === 1) { // RESULT_KEY_CAPTURED
                hardwarePttKeyCodeField.text = page.learnedHardwarePttKeyCodeText()
            }
        }
    }

    Dialog {
        id: pttLearningDialog
        objectName: "pttLearningDialog"
        anchors.centerIn: parent
        title: qsTr("Learn PTT Button")
        modal: true
        standardButtons: Dialog.Cancel
        closePolicy: Popup.CloseOnEscape
        contentWidth: pttLearningContent.implicitWidth
        contentHeight: pttLearningContent.implicitHeight

        onRejected: page.reflectorClient.cancelHardwarePttLearning()

        contentItem: ColumnLayout {
            id: pttLearningContent
            spacing: 16
            implicitWidth: 280

            Label {
                Layout.fillWidth: true
                text: qsTr("Press a hardware button, or press the PTT button on a paired Bluetooth device.")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: page.reflectorClient.hardwarePttLearningActive
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Waiting... (15 seconds)")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: "#556070"
                font.pixelSize: 12
            }
        }
    }

    background: Rectangle {
        Accessible.ignored: true

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f7f9fd" }
            GradientStop { position: 1.0; color: "#eef3fb" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: page.contentPadding + page.safeAreaTop
        anchors.leftMargin: page.contentPadding + page.safeAreaLeft
        anchors.rightMargin: page.contentPadding + page.safeAreaRight
        anchors.bottomMargin: page.contentPadding + page.safeAreaBottom
        spacing: page.uiMetrics.pageSpacing

        Item {
            Layout.fillWidth: true
            implicitHeight: Math.max(backButton.implicitHeight, titleColumn.implicitHeight)

            ToolButton {
                id: backButton

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2190"
                font.pixelSize: page.uiMetrics.pageTitleFontSize
                padding: page.compactSettingsMode ? 4 : 6
                Accessible.name: qsTr("Back")
                Accessible.description: page.compactSettingsMode && page.compactSection !== ""
                                        ? qsTr("Return to the settings section list")
                                        : qsTr("Return to the main screen")
                onClicked: {
                    if (page.compactSettingsMode && page.compactSection !== "") {
                        page.compactSection = ""
                        return
                    }
                    page.backRequested()
                }
            }

            Column {
                id: titleColumn

                anchors.centerIn: parent
                width: Math.max(0, parent.width - (backButton.implicitWidth * 2) - 16)
                spacing: 0

                Label {
                    width: parent.width
                    text: page.compactSettingsMode && page.compactSection !== ""
                          ? page.compactSectionTitle(page.compactSection)
                          : qsTr("Settings")
                    font.pixelSize: page.uiMetrics.pageTitleFontSize
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }

        ScrollView {
            id: settingsScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Settings content")

            Column {
                width: settingsScroll.availableWidth
                spacing: page.compactSettingsMode ? 8 : 12

                Frame {
                    id: compactSectionsFrame
                    objectName: "compactSectionsFrame"
                    visible: page.compactSectionChooserVisible
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Compact settings sections")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: page.compactSettingsMode ? 8 : 10

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Quick Settings")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Small rugged screens show one settings area at a time so the radio controls stay readable.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            font.pixelSize: page.uiMetrics.bodyFontSize
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        QuickSwitchTile {
                            Layout.fillWidth: true
                            compact: true
                            labelText: qsTr("Profiles")
                            primaryText: page.profilesModel.count > 0
                                         ? qsTr("%1 saved").arg(page.profilesModel.count)
                                         : qsTr("No profiles")
                            secondaryText: qsTr("Server list and profile switching")
                            accentColor: page.accentColor
                            emphasized: true
                            onClicked: page.compactSection = "profiles"
                        }

                        QuickSwitchTile {
                            Layout.fillWidth: true
                            compact: true
                            labelText: qsTr("Audio")
                            primaryText: page.audioRouteName(page.currentAudioRoute)
                            secondaryText: qsTr("Levels and Android routing")
                            accentColor: page.accentColor
                            onClicked: page.compactSection = "audio"
                        }

                        QuickSwitchTile {
                            Layout.fillWidth: true
                            compact: true
                            labelText: qsTr("Radio")
                            primaryText: qsTr("TOT %1s").arg(page.txTimeoutSeconds)
                            secondaryText: qsTr("PTT hang %1 ms").arg(page.pttHangTimeMs)
                            accentColor: page.accentColor
                            onClicked: page.compactSection = "radio"
                        }

                        QuickSwitchTile {
                            Layout.fillWidth: true
                            compact: true
                            labelText: qsTr("Device")
                            primaryText: qsTr("Background service")
                            secondaryText: qsTr("Battery guide and hardware PTT")
                            accentColor: page.accentColor
                            onClicked: page.compactSection = "device"
                        }

                        QuickSwitchTile {
                            Layout.fillWidth: true
                            compact: true
                            labelText: qsTr("Node Info")
                            primaryText: qsTr("%1 custom").arg(page.nodeInfoPropertiesModel.count)
                            secondaryText: qsTr("Read-only and custom node_info.json fields")
                            accentColor: page.accentColor
                            onClicked: page.compactSection = "nodeinfo"
                        }
                    }
                }

                Rectangle {
                    visible: (!page.reflectorClient.isDisconnected || page.profileSwitchInProgress)
                             && (!page.compactSettingsMode
                                 || page.compactSection === ""
                                 || page.compactSection === "profiles")
                    width: parent.width
                    radius: page.uiMetrics.frameRadius
                    color: "#fff6dd"
                    border.color: "#f0d480"
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Connection warning")
                    Accessible.description: qsTr("Switching profiles reconnects the radio session")

                    Column {
                        anchors.fill: parent
                        anchors.margins: page.uiMetrics.sectionPadding
                        spacing: page.compactSettingsMode ? 4 : 6

                        Label {
                            width: parent.width
                            text: page.profileSwitchInProgress
                                  ? qsTr("Switching profiles...")
                                  : qsTr("Switching profile reconnects the radio.")
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            width: parent.width
                            text: page.profileSwitchInProgress
                                  ? qsTr("The app disconnects first, then reconnects with the new server profile.")
                                  : qsTr("Tap another profile below to reconnect using it. The active profile cannot be deleted until you disconnect.")
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "profiles"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Server profiles")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: Column {
                        id: serverProfilesColumn

                        spacing: page.compactSettingsMode ? 10 : 12

                        RowLayout {
                            width: parent.width

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Server Profiles")
                                font.pixelSize: page.uiMetrics.sectionTitleFontSize
                                font.bold: true
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            Button {
                                visible: page.profilesModel.count > 0
                                text: qsTr("Add")
                                Accessible.name: qsTr("Add profile")
                                Accessible.description: qsTr("Create a new server profile")
                                onClicked: page.addProfileRequested()
                            }
                        }

                        Label {
                            width: parent.width
                            visible: page.profilesModel.count > 0
                            text: qsTr("Tap a profile card to make it active. Use Edit to adjust its server details.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Rectangle {
                            visible: page.profilesModel.count === 0
                            width: parent.width
                            radius: page.uiMetrics.nestedFrameRadius
                            color: "#f8fafc"
                            border.color: "#d7deee"
                            height: emptyProfilesColumn.implicitHeight + (page.compactSettingsMode ? 24 : 28)
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("No saved profiles")

                            Column {
                                id: emptyProfilesColumn

                                anchors.centerIn: parent
                                width: parent.width - 28
                                spacing: page.compactSettingsMode ? 6 : 8

                                Label {
                                    width: parent.width
                                    text: qsTr("No server profiles saved yet.")
                                    horizontalAlignment: Text.AlignHCenter
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Label {
                                    width: parent.width
                                    text: qsTr("Add your first server profile to make connect and reconnect one tap.")
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    width: parent.width
                                    text: qsTr("Add Profile")
                                    Accessible.name: qsTr("Add profile")
                                    onClicked: page.addProfileRequested()
                                }
                            }
                        }

                        Repeater {
                            model: page.profilesModel

                            delegate: ProfileCard {
                                width: parent.width
                                compact: page.compactSettingsMode
                                isSelected: profileId === page.selectedProfileId
                                connected: !page.reflectorClient.isDisconnected
                                canUse: profileId !== page.selectedProfileId
                                        && !page.profileSwitchInProgress
                                        && !page.reflectorClient.pttActive
                                        && (page.reflectorClient.isDisconnected
                                            || page.reflectorClient.connectionStatus.startsWith("Connected"))
                                canDelete: !page.profileSwitchInProgress
                                           && (page.reflectorClient.isDisconnected
                                               || profileId !== page.selectedProfileId)
                                tintedSurfaceColor: page.tintedSurfaceColor
                                accentColor: page.accentColor

                                onActivateRequested: function(profileId) {
                                    page.selectProfileRequested(profileId)
                                }

                                onEditRequested: function(editIndex, draftProfile) {
                                    page.editProfileRequested(editIndex, draftProfile)
                                }

                                onDeleteRequested: function(editIndex) {
                                    page.deleteProfileRequested(editIndex)
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "audio"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Audio levels")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Audio Levels")
                                font.pixelSize: page.uiMetrics.sectionTitleFontSize
                                font.bold: true
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            Button {
                                visible: page.hasCustomAudioLevels()
                                text: qsTr("Reset")
                                Accessible.name: qsTr("Reset audio levels")
                                onClicked: {
                                    page.rxAudioLevelRequested(0.0)
                                    page.txAudioLevelRequested(0.0)
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Use device volume for normal loudness. These controls fine-tune receive boost and microphone trim.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Receive audio boost")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Receive Boost")
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        text: page.rxAudioLevelText(rxLevelSlider.value)
                                        font.bold: true
                                        color: page.accentColor
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.isZeroLevel(rxLevelSlider.value)
                                          ? qsTr("Leave this off unless incoming audio is still too quiet after raising device volume.")
                                          : qsTr("%1 receive boost").arg(page.signedDbText(rxLevelSlider.value, 1))
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Slider {
                                    id: rxLevelSlider
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 9.0
                                    stepSize: 0.5
                                    snapMode: Slider.SnapAlways
                                    value: 0.0
                                    Accessible.name: qsTr("Receive boost")
                                    onMoved: page.rxAudioLevelRequested(value)
                                    onPressedChanged: {
                                        if (!pressed)
                                            page.rxAudioLevelRequested(value)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: qsTr("Off")
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: qsTr("+9 dB")
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }
                            }
                        }

                        Frame {
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Microphone level")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Mic Level")
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        text: page.txAudioLevelText(txLevelSlider.value)
                                        font.bold: true
                                        color: page.accentColor
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.isZeroLevel(txLevelSlider.value)
                                          ? qsTr("Leave this at 0 dB unless other stations report your audio level is off.")
                                          : qsTr("%1 mic trim").arg(page.signedDbText(txLevelSlider.value, 0))
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Slider {
                                    id: txLevelSlider
                                    Layout.fillWidth: true
                                    from: -12.0
                                    to: 12.0
                                    stepSize: 1.0
                                    snapMode: Slider.SnapAlways
                                    value: 0.0
                                    Accessible.name: qsTr("Microphone level")
                                    onMoved: page.txAudioLevelRequested(value)
                                    onPressedChanged: {
                                        if (!pressed)
                                            page.txAudioLevelRequested(value)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        text: qsTr("-12 dB")
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: qsTr("+12 dB")
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: txLevelSlider.value > 6.0
                                    text: qsTr("Higher mic trim can add distortion.")
                                    wrapMode: Text.WordWrap
                                    color: "#9a5b00"
                                    font.bold: true
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: Qt.platform.os === "android"
                             && (!page.compactSettingsMode || page.compactSection === "audio")
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Audio routing")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 8

                        Label {
                            text: qsTr("Audio Routing")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Current route: %1").arg(page.audioRouteName(page.currentAudioRoute))
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: page.preferredAudioRoute !== page.currentAudioRoute
                                     && !page.audioRouteAvailable(page.preferredAudioRoute)
                            text: qsTr("%1 will be used when available.")
                                    .arg(page.audioRouteName(page.preferredAudioRoute))
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: page.reflectorClient.pttActive
                            text: qsTr("Release PTT to change route.")
                            wrapMode: Text.WordWrap
                            color: "#9a5b00"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            id: speakerRouteCard
                            visible: page.audioRouteAvailable("speaker")
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: page.audioRouteName("speaker")

                            readonly property bool preferred: page.preferredAudioRoute === "speaker"
                            readonly property bool current: page.currentAudioRoute === "speaker"

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: speakerRouteCard.preferred ? "#edf2ff" : "#f8fafc"
                                border.color: speakerRouteCard.preferred ? page.accentColor : "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: page.audioRouteName("speaker")
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        visible: speakerRouteCard.current
                                        text: qsTr("Current")
                                        color: "#2f5b16"
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        visible: speakerRouteCard.preferred
                                        text: qsTr("Selected")
                                        color: page.accentColor
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.audioRouteDescription("speaker")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    Layout.fillWidth: true
                                    text: (speakerRouteCard.preferred && speakerRouteCard.current)
                                          ? qsTr("Active") : qsTr("Use This Route")
                                    enabled: !(speakerRouteCard.preferred && speakerRouteCard.current)
                                             && !page.reflectorClient.pttActive
                                    Accessible.name: (speakerRouteCard.preferred && speakerRouteCard.current)
                                                     ? qsTr("%1 active").arg(page.audioRouteName("speaker"))
                                                     : qsTr("Use %1").arg(page.audioRouteName("speaker"))
                                    onClicked: page.audioRouteRequested("speaker")
                                }
                            }
                        }

                        Frame {
                            id: wiredRouteCard
                            visible: page.audioRouteAvailable("wired_headset")
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: page.audioRouteName("wired_headset")

                            readonly property bool preferred: page.preferredAudioRoute === "wired_headset"
                            readonly property bool current: page.currentAudioRoute === "wired_headset"

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: wiredRouteCard.preferred ? "#edf2ff" : "#f8fafc"
                                border.color: wiredRouteCard.preferred ? page.accentColor : "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: page.audioRouteName("wired_headset")
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        visible: wiredRouteCard.current
                                        text: qsTr("Current")
                                        color: "#2f5b16"
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        visible: wiredRouteCard.preferred
                                        text: qsTr("Selected")
                                        color: page.accentColor
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.audioRouteDescription("wired_headset")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    Layout.fillWidth: true
                                    text: (wiredRouteCard.preferred && wiredRouteCard.current)
                                          ? qsTr("Active") : qsTr("Use This Route")
                                    enabled: !(wiredRouteCard.preferred && wiredRouteCard.current)
                                             && !page.reflectorClient.pttActive
                                    Accessible.name: (wiredRouteCard.preferred && wiredRouteCard.current)
                                                     ? qsTr("%1 active").arg(page.audioRouteName("wired_headset"))
                                                     : qsTr("Use %1").arg(page.audioRouteName("wired_headset"))
                                    onClicked: page.audioRouteRequested("wired_headset")
                                }
                            }
                        }

                        Frame {
                            id: bluetoothRouteCard
                            visible: page.audioRouteAvailable("bluetooth")
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: page.audioRouteName("bluetooth")

                            readonly property bool preferred: page.preferredAudioRoute === "bluetooth"
                            readonly property bool current: page.currentAudioRoute === "bluetooth"

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: bluetoothRouteCard.preferred ? "#edf2ff" : "#f8fafc"
                                border.color: bluetoothRouteCard.preferred ? page.accentColor : "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true

                                    Label {
                                        Layout.fillWidth: true
                                        text: page.audioRouteName("bluetooth")
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Image {
                                        source: "qrc:/icons/bluetooth.svg"
                                        sourceSize: Qt.size(20, 20)
                                        visible: status === Image.Ready
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.audioRouteDescription("bluetooth")
                                    color: "#556070"
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    Layout.fillWidth: true
                                    text: (bluetoothRouteCard.preferred && bluetoothRouteCard.current)
                                          ? qsTr("Active") : qsTr("Use This Route")
                                    enabled: !(bluetoothRouteCard.preferred && bluetoothRouteCard.current)
                                             && !page.reflectorClient.pttActive
                                    Accessible.name: (bluetoothRouteCard.preferred && bluetoothRouteCard.current)
                                                     ? qsTr("%1 active").arg(page.audioRouteName("bluetooth"))
                                                     : qsTr("Use %1").arg(page.audioRouteName("bluetooth"))
                                    onClicked: page.audioRouteRequested("bluetooth")
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "radio"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("On-screen PTT")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        Label {
                            text: qsTr("On-Screen PTT")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Show the Tap to Talk button on the Home screen. Turn it off when hardware PTT is your primary transmit control.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        GridLayout {
                            columns: page.compactSettingsMode ? 1 : 2
                            rowSpacing: page.compactSettingsMode ? 8 : 0
                            columnSpacing: 12
                            Layout.fillWidth: true

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Show Tap to Talk Button")
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.tapToTalkButtonVisible
                                          ? qsTr("The Home screen includes the software PTT button.")
                                          : qsTr("The Home screen keeps the software PTT button hidden.")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }

                            Switch {
                                id: tapToTalkButtonVisibleSwitch
                                objectName: "tapToTalkButtonVisibleSwitch"
                                checked: page.tapToTalkButtonVisible
                                Layout.alignment: page.compactSettingsMode
                                                  ? Qt.AlignLeft
                                                  : (Qt.AlignRight | Qt.AlignVCenter)
                                Accessible.name: qsTr("Show Tap to Talk button")
                                onClicked: page.tapToTalkButtonVisibleRequested(checked)
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "radio"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("PTT hang time")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        Label {
                            text: qsTr("PTT Hang Time")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Keep TX alive briefly after release so the last syllable is not clipped. Enter 0 to disable. Maximum 1000 ms.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("PTT hang time milliseconds")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                TextField {
                                    id: pttHangTimeField

                                    Layout.fillWidth: true
                                    text: String(page.pttHangTimeMs)
                                    placeholderText: qsTr("PTT hang time (ms)")
                                    validator: IntValidator { bottom: 0; top: 1000 }
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    Accessible.name: qsTr("PTT hang time milliseconds")

                                    onEditingFinished: {
                                        const trimmedText = text.trim()
                                        if (acceptableInput && trimmedText !== "") {
                                            page.pttHangTimeMsRequested(parseInt(trimmedText, 10))
                                        } else {
                                            text = String(page.pttHangTimeMs)
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Current value: %1 ms").arg(page.pttHangTimeMs)
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "radio"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Transmit timeout")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        Label {
                            text: qsTr("Transmit Timeout")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Latry stops TX automatically after this many seconds. Enter a whole number greater than zero.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Transmit timeout seconds")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                TextField {
                                    id: txTimeoutField

                                    Layout.fillWidth: true
                                    text: String(page.txTimeoutSeconds)
                                    placeholderText: qsTr("TX timeout (s)")
                                    validator: IntValidator { bottom: 1 }
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    Accessible.name: qsTr("Transmit timeout seconds")

                                    onEditingFinished: {
                                        const trimmedText = text.trim()
                                        if (acceptableInput && trimmedText !== "") {
                                            page.txTimeoutSecondsRequested(parseInt(trimmedText, 10))
                                        } else {
                                            text = String(page.txTimeoutSeconds)
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Current value: %1 seconds").arg(page.txTimeoutSeconds)
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "device"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Android settings")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        id: androidSettingsColumn

                        spacing: 12

                        Label {
                            text: qsTr("Android Settings")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Android-specific controls for rugged side buttons, media-button PTT, and background service behavior.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            visible: Qt.platform.os === "android"
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Hardware PTT settings")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 10

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Hardware PTT")
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Standard PTT broadcasts and dedicated PTT buttons always work. Enable the rugged profile to also accept common side buttons as PTT, or learn a specific button below.")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                GridLayout {
                                    columns: page.stackedHardwarePttControls ? 1 : 2
                                    rowSpacing: page.stackedHardwarePttControls ? 8 : 0
                                    columnSpacing: 12
                                    Layout.fillWidth: true

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("Enable Rugged Fixed-Key Profile")
                                            font.bold: true
                                            wrapMode: Text.WordWrap
                                            Accessible.role: Accessible.StaticText
                                            Accessible.name: text
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("Use common rugged side-button media and gamepad keys as PTT when the app is active.")
                                            wrapMode: Text.WordWrap
                                            color: "#556070"
                                            Accessible.role: Accessible.StaticText
                                            Accessible.name: text
                                        }
                                    }

                                    Switch {
                                        id: hardwarePttEnabledSwitch
                                        objectName: "hardwarePttEnabledSwitch"
                                        checked: page.reflectorClient.hardwarePttEnabled
                                        Layout.alignment: page.stackedHardwarePttControls
                                                          ? Qt.AlignLeft
                                                          : (Qt.AlignRight | Qt.AlignVCenter)
                                        Accessible.name: qsTr("Enable rugged fixed-key hardware PTT")
                                        onClicked: page.hardwarePttEnabledRequested(checked)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Learned PTT Button")
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Teach the app which physical button to use as PTT. Press Learn, then press the hardware button you want to bind.")
                                        wrapMode: Text.WordWrap
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    GridLayout {
                                        id: hardwarePttActionLayout
                                        objectName: "hardwarePttActionLayout"
                                        Layout.fillWidth: true
                                        columns: page.stackedHardwarePttControls ? 1 : 2
                                        rowSpacing: 8
                                        columnSpacing: 8

                                        Button {
                                            id: learnPttButton
                                            objectName: "learnPttButton"
                                            Layout.fillWidth: true
                                            text: page.reflectorClient.hardwarePttLearningActive
                                                  ? (page.stackedHardwarePttControls
                                                     ? qsTr("Waiting...")
                                                     : qsTr("Waiting for key..."))
                                                  : (page.stackedHardwarePttControls
                                                     ? qsTr("Learn")
                                                     : qsTr("Learn PTT Button"))
                                            enabled: !page.reflectorClient.hardwarePttLearningActive
                                            Accessible.name: qsTr("Start hardware PTT key learning")
                                            Accessible.description: qsTr("Press this, then press the physical button you want to use as PTT")
                                            onClicked: {
                                                page.reflectorClient.startHardwarePttLearning()
                                                pttLearningDialog.open()
                                            }
                                        }

                                        Button {
                                            objectName: "clearHardwarePttKeyCodeButton"
                                            Layout.fillWidth: page.stackedHardwarePttControls
                                            Layout.alignment: page.stackedHardwarePttControls
                                                              ? Qt.AlignLeft
                                                              : Qt.AlignVCenter
                                            text: qsTr("Clear")
                                            enabled: (page.reflectorClient.learnedHardwarePttKeyCode > 0
                                                      || SppPttController.learnedSppDeviceAddress !== "")
                                                     && !page.reflectorClient.hardwarePttLearningActive
                                            onClicked: {
                                                if (SppPttController.learnedSppDeviceAddress !== "")
                                                    SppPttController.clearLearnedSppDevice()
                                                else
                                                    page.clearLearnedHardwarePttKeyCodeRequested()
                                            }
                                        }
                                    }

                                    Label {
                                        id: learnedKeyStatusLabel
                                        objectName: "learnedKeyStatusLabel"
                                        Layout.fillWidth: true
                                        text: {
                                            if (page.reflectorClient.hardwarePttLearningActive)
                                                return qsTr("Press a hardware button or activate a Bluetooth SPP PTT device now...")
                                            if (SppPttController.learnedSppDeviceAddress !== "")
                                                return qsTr("SPP device: %1").arg(SppPttController.learnedSppDeviceName)
                                            if (page.reflectorClient.learnedHardwarePttKeyCode > 0)
                                                return qsTr("Learned key code: %1").arg(page.reflectorClient.learnedHardwarePttKeyCode)
                                            return qsTr("No learned key saved.")
                                        }
                                        wrapMode: Text.WordWrap
                                        color: page.reflectorClient.hardwarePttLearningActive ? "#2563eb" : "#556070"
                                        font.italic: page.reflectorClient.hardwarePttLearningActive
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    GridLayout {
                                        id: hardwarePttManualKeyLayout
                                        objectName: "hardwarePttManualKeyLayout"
                                        Layout.fillWidth: true
                                        columns: page.stackedHardwarePttControls ? 1 : 2
                                        rowSpacing: 6
                                        columnSpacing: 8

                                        Label {
                                            objectName: "hardwarePttManualKeyLabel"
                                            Layout.fillWidth: true
                                            text: qsTr("Manual key code:")
                                            color: "#556070"
                                        }

                                        TextField {
                                            id: hardwarePttKeyCodeField
                                            objectName: "hardwarePttKeyCodeField"
                                            Layout.fillWidth: true
                                            placeholderText: page.stackedHardwarePttControls
                                                             ? qsTr("88")
                                                             : qsTr("e.g. 88")
                                            validator: IntValidator { bottom: 1 }
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            enabled: !page.reflectorClient.hardwarePttLearningActive
                                            Accessible.name: qsTr("Learned hardware PTT key code")

                                            onEditingFinished: {
                                                const trimmedText = text.trim()
                                                if (trimmedText === "") {
                                                    page.clearLearnedHardwarePttKeyCodeRequested()
                                                    return
                                                }

                                                if (acceptableInput) {
                                                    page.learnedHardwarePttKeyCodeRequested(parseInt(trimmedText, 10))
                                                } else {
                                                    text = page.learnedHardwarePttKeyCodeText()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Frame {
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Background service settings")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Background Service")
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("If Android kills the app in the background, open the guide and review the battery settings for Latry.")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    text: qsTr("Open Battery Guide")
                                    Accessible.name: qsTr("Battery optimization help")
                                    Accessible.description: qsTr("Show instructions for keeping the background service active")
                                    onClicked: page.batteryOptimizationRequested()
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: page.liveTranscriptionSettingsVisible
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Live transcription")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        Label {
                            text: qsTr("Live Transcription")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Convert received audio into on-screen text with Android's on-device recognizer. Latry only uses installed on-device models and keeps TX microphone capture separate.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Frame {
                            visible: page.reflectorClient.transcriptionAvailable
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Live transcription switch")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: RowLayout {
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Enable RX Transcription")
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Latry feeds the decoded RX stream directly into Android speech recognition while playback continues normally.")
                                        wrapMode: Text.WordWrap
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }

                                Switch {
                                    checked: page.reflectorClient.liveTranscriptionEnabled
                                    Accessible.name: qsTr("Enable receive transcription")
                                    onClicked: page.liveTranscriptionEnabledRequested(checked)
                                }
                            }
                        }

                        Frame {
                            visible: page.reflectorClient.transcriptionDownloadableLanguages.length > 0
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Available speech languages")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Available Speech Languages")
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Button {
                                        text: page.downloadableLanguagesExpanded
                                              ? qsTr("Hide")
                                              : qsTr("Show (%1)")
                                                    .arg(page.reflectorClient.transcriptionDownloadableLanguages.length)
                                        Accessible.name: page.downloadableLanguagesExpanded
                                                         ? qsTr("Collapse available speech languages")
                                                         : qsTr("Expand available speech languages")
                                        onClicked: page.downloadableLanguagesExpanded = !page.downloadableLanguagesExpanded
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: page.reflectorClient.transcriptionModelDownloadStatus !== ""
                                          ? page.reflectorClient.transcriptionModelDownloadStatus
                                          : qsTr("Download the language models you want Android to use for live transcription.")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                ColumnLayout {
                                    visible: page.downloadableLanguagesExpanded
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Repeater {
                                        model: page.reflectorClient.transcriptionDownloadableLanguages

                                        delegate: Frame {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            padding: 10
                                            Accessible.role: Accessible.Grouping
                                            Accessible.name: modelData.name

                                            background: Rectangle {
                                                Accessible.ignored: true
                                                radius: 14
                                                color: "#ffffff"
                                                border.color: "#d7deee"
                                            }

                                            contentItem: RowLayout {
                                                spacing: 12

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.name
                                                        font.bold: true
                                                        wrapMode: Text.WordWrap
                                                        Accessible.role: Accessible.StaticText
                                                        Accessible.name: text
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.tag + " · " + modelData.status
                                                        wrapMode: Text.WordWrap
                                                        color: "#556070"
                                                        Accessible.role: Accessible.StaticText
                                                        Accessible.name: text
                                                    }
                                                }

                                                Button {
                                                    text: modelData.pending || modelData.activeDownload
                                                          ? qsTr("Downloading…")
                                                          : qsTr("Download")
                                                    enabled: !page.reflectorClient.transcriptionModelDownloadInProgress
                                                             && !modelData.pending
                                                             && !modelData.activeDownload
                                                    Accessible.name: qsTr("Download %1 speech model").arg(modelData.name)
                                                    onClicked: page.transcriptionLanguageDownloadRequested(modelData.tag)
                                                }
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    BusyIndicator {
                                        running: page.reflectorClient.transcriptionModelDownloadInProgress
                                        visible: running
                                        Accessible.name: qsTr("Speech model download in progress")
                                    }

                                    Label {
                                        visible: page.reflectorClient.transcriptionModelDownloadProgress >= 0
                                                 && page.reflectorClient.transcriptionModelDownloadInProgress
                                        text: qsTr("%1% complete")
                                                .arg(page.reflectorClient.transcriptionModelDownloadProgress)
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        Frame {
                            visible: page.reflectorClient.transcriptionInstalledLanguages.length > 0
                            Layout.fillWidth: true
                            padding: page.uiMetrics.nestedSectionPadding
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("Installed speech languages")

                            background: Rectangle {
                                Accessible.ignored: true
                                radius: page.uiMetrics.nestedFrameRadius
                                color: "#f8fafc"
                                border.color: "#d7deee"
                            }

                            contentItem: ColumnLayout {
                                spacing: 10

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Installed Speech Languages")
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Repeater {
                                    model: page.reflectorClient.transcriptionInstalledLanguages

                                    delegate: Frame {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        padding: 10
                                        Accessible.role: Accessible.Grouping
                                        Accessible.name: modelData.name

                                        background: Rectangle {
                                            Accessible.ignored: true
                                            radius: 14
                                            color: "#ffffff"
                                            border.color: "#d7deee"
                                        }

                                        contentItem: ColumnLayout {
                                            spacing: 2

                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                                Accessible.role: Accessible.StaticText
                                                Accessible.name: text
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.tag + " · " + modelData.status
                                                wrapMode: Text.WordWrap
                                                color: "#556070"
                                                Accessible.role: Accessible.StaticText
                                                Accessible.name: text
                                            }
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Android does not expose app-level removal APIs for speech models. Use Android voice input settings to manage or remove installed languages.")
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Button {
                                    text: qsTr("Open Android Settings")
                                    Accessible.name: qsTr("Open Android voice input settings")
                                    onClicked: page.transcriptionLanguageSettingsRequested()
                                }
                            }
                        }
                    }
                }

                Frame {
                    visible: !page.compactSettingsMode || page.compactSection === "nodeinfo"
                    width: parent.width
                    padding: page.uiMetrics.sectionPadding
                    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("node_info.json settings")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: page.uiMetrics.frameRadius
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    contentItem: ColumnLayout {
                        spacing: 12

                        Label {
                            text: qsTr("node_info.json")
                            font.pixelSize: page.uiMetrics.sectionTitleFontSize
                            font.bold: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Latry always writes the read-only fields below into node_info.json. Callsign is taken from the selected profile.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Repeater {
                            model: page.nodeInfoReadOnlyEntries

                            delegate: Frame {
                                id: readOnlyNodeInfoCard

                                required property var modelData

                                Layout.fillWidth: true
                                padding: page.uiMetrics.nestedSectionPadding
                                Accessible.role: Accessible.Grouping
                                Accessible.name: qsTr("Read only node info property %1").arg(modelData.key)

                                background: Rectangle {
                                    Accessible.ignored: true
                                    radius: page.uiMetrics.nestedFrameRadius
                                    color: "#f8fafc"
                                    border.color: "#d7deee"
                                }

                                contentItem: ColumnLayout {
                                    spacing: 6

                                    Label {
                                        Layout.fillWidth: true
                                        text: readOnlyNodeInfoCard.modelData.key
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: String(readOnlyNodeInfoCard.modelData.value)
                                        textFormat: readOnlyNodeInfoCard.modelData.key === "tip"
                                                    ? Text.RichText
                                                    : Text.PlainText
                                        wrapMode: Text.Wrap
                                        color: "#334155"
                                        onLinkActivated: link => Qt.openUrlExternally(link)
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Custom Properties")
                                font.pixelSize: 18
                                font.bold: true
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            Button {
                                text: qsTr("Add")
                                Accessible.name: qsTr("Add node info property")
                                onClicked: page.addNodeInfoPropertyRequested()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Add extra key/value pairs to include in node_info.json. Reserved keys stay read-only.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Rectangle {
                            visible: page.nodeInfoPropertiesModel.count === 0
                            Layout.fillWidth: true
                            radius: page.uiMetrics.nestedFrameRadius
                            color: "#f8fafc"
                            border.color: "#d7deee"
                            implicitHeight: emptyNodeInfoColumn.implicitHeight + (page.compactSettingsMode ? 24 : 28)
                            Accessible.role: Accessible.Grouping
                            Accessible.name: qsTr("No custom node info properties")

                            Column {
                                id: emptyNodeInfoColumn

                                anchors.centerIn: parent
                                width: parent.width - 28
                                spacing: page.compactSettingsMode ? 6 : 8

                                Label {
                                    width: parent.width
                                    text: qsTr("No custom node info properties yet.")
                                    horizontalAlignment: Text.AlignHCenter
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }

                                Label {
                                    width: parent.width
                                    text: qsTr("Add optional properties if you want extra fields in node_info.json.")
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: "#556070"
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }
                        }

                        Repeater {
                            model: page.nodeInfoPropertiesModel

                            delegate: Frame {
                                id: customNodeInfoCard

                                required property int index
                                required property string propertyKey
                                required property string propertyValue

                                readonly property bool reservedKey: page.isReservedNodeInfoKey(propertyKey)
                                readonly property bool incomplete: String(propertyKey).trim() === ""
                                                                 || String(propertyValue).trim() === ""
                                readonly property bool readyToSend: !reservedKey && !incomplete

                                Layout.fillWidth: true
                                padding: page.uiMetrics.nestedSectionPadding
                                Accessible.role: Accessible.Grouping
                                Accessible.name: qsTr("Custom node info property %1").arg(index + 1)

                                background: Rectangle {
                                    Accessible.ignored: true
                                    radius: page.uiMetrics.nestedFrameRadius
                                    color: "#f8fafc"
                                    border.color: customNodeInfoCard.reservedKey ? "#f0d480" : "#d7deee"
                                }

                                contentItem: ColumnLayout {
                                    spacing: 8

                                    TextField {
                                        id: propertyKeyField

                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Property name")
                                        text: customNodeInfoCard.propertyKey
                                        Accessible.name: qsTr("Node info property name %1").arg(customNodeInfoCard.index + 1)
                                        onTextEdited: page.updateNodeInfoPropertyRequested(customNodeInfoCard.index,
                                                                                          text,
                                                                                          propertyValueField.text)
                                    }

                                    TextField {
                                        id: propertyValueField

                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Property value")
                                        text: customNodeInfoCard.propertyValue
                                        Accessible.name: qsTr("Node info property value %1").arg(customNodeInfoCard.index + 1)
                                        onTextEdited: page.updateNodeInfoPropertyRequested(customNodeInfoCard.index,
                                                                                          propertyKeyField.text,
                                                                                          text)
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: customNodeInfoCard.reservedKey
                                        text: qsTr("Reserved key. sw, swVer, tip, Website and callsign are managed by Latry.")
                                        wrapMode: Text.WordWrap
                                        color: "#9a5b00"
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: !customNodeInfoCard.reservedKey && customNodeInfoCard.incomplete
                                        text: qsTr("Both name and value are required before this property will be sent.")
                                        wrapMode: Text.WordWrap
                                        color: "#556070"
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: customNodeInfoCard.readyToSend
                                        text: qsTr("Saved automatically. This property will be sent in node_info.json.")
                                        wrapMode: Text.WordWrap
                                        color: "#2f5b16"
                                        font.bold: true
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Item {
                                            Layout.fillWidth: true
                                            Accessible.ignored: true
                                        }

                                        Button {
                                            text: qsTr("Delete")
                                            Accessible.name: qsTr("Delete custom node info property %1").arg(customNodeInfoCard.index + 1)
                                            onClicked: page.deleteNodeInfoPropertyRequested(customNodeInfoCard.index)
                                        }
                                    }
                                }
                            }
                        }

                        Button {
                            visible: page.nodeInfoPropertiesModel.count > 0
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Add")
                            Accessible.name: qsTr("Add node info property")
                            onClicked: page.addNodeInfoPropertyRequested()
                        }
                    }
                }

            }
        }
    }
}
