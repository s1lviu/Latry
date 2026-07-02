import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page

    required property real contentPadding
    required property real safeAreaTop
    required property real safeAreaLeft
    required property real safeAreaRight
    required property real safeAreaBottom
    required property color surfaceColor
    required property color borderColor
    required property int editIndex
    required property var draftProfile

    property string draftName: page.draftProfile && page.draftProfile.name ? page.draftProfile.name : ""
    property string draftHost: page.draftProfile && page.draftProfile.host ? page.draftProfile.host : ""
    property string draftPort: page.draftProfile && page.draftProfile.port ? String(page.draftProfile.port) : "5300"
    property string draftCallsign: page.draftProfile && page.draftProfile.callsign ? page.draftProfile.callsign : ""
    property string draftAuthKey: page.draftProfile && page.draftProfile.authKey ? page.draftProfile.authKey : ""
    property string draftTalkgroup: page.draftProfile && page.draftProfile.talkgroup !== undefined ? String(page.draftProfile.talkgroup) : "9"
    property string draftTgSelectTimeout: page.draftProfile && page.draftProfile.tgSelectTimeout !== undefined
                                          ? String(page.draftProfile.tgSelectTimeout)
                                          : "30"
    property string draftMonitoredTalkgroups: page.draftProfile && page.draftProfile.monitoredTalkgroups
                                              ? page.draftProfile.monitoredTalkgroups
                                              : ""

    readonly property bool formComplete: String(page.draftHost).trim() !== ""
                                       && String(page.draftPort).trim() !== ""
                                       && String(page.draftCallsign).trim() !== ""
                                       && String(page.draftAuthKey).trim() !== ""
                                       && String(page.draftTalkgroup).trim() !== ""
    signal backRequested()
    signal saveRequested(int editIndex, var profileData)

    Accessible.role: Accessible.Pane
    Accessible.name: page.editIndex >= 0 ? qsTr("Edit profile") : qsTr("New profile")

    function currentProfilePayload() {
        return {
            profileId: page.draftProfile && page.draftProfile.profileId ? page.draftProfile.profileId : "",
            name: page.draftName,
            host: page.draftHost,
            port: page.draftPort,
            callsign: page.draftCallsign,
            authKey: page.draftAuthKey,
            talkgroup: page.draftTalkgroup,
            monitoredTalkgroups: page.draftMonitoredTalkgroups,
            tgSelectTimeout: page.draftTgSelectTimeout
        }
    }

    function requestSave() {
        InputMethod.commit()
        if (page.formComplete)
            page.saveRequested(page.editIndex, page.currentProfilePayload())
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
        spacing: 12

        Item {
            Layout.fillWidth: true
            implicitHeight: Math.max(backButton.implicitHeight, titleLabel.implicitHeight)

            ToolButton {
                id: backButton

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2190"
                font.pixelSize: 24
                padding: 6
                Accessible.name: qsTr("Back")
                Accessible.description: qsTr("Return to settings")
                onClicked: page.backRequested()
            }

            Label {
                id: titleLabel

                anchors.centerIn: parent
                width: Math.max(0, parent.width - (backButton.implicitWidth * 2) - 16)
                text: page.editIndex >= 0 ? qsTr("Edit Profile") : qsTr("New Profile")
                font.pixelSize: 24
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        ScrollView {
            id: editorScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Column {
                width: editorScroll.availableWidth
                spacing: 12

                Frame {
                    width: parent.width
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Profile form")

                    background: Rectangle {
                        Accessible.ignored: true
                        radius: 20
                        color: page.surfaceColor
                        border.color: page.borderColor
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Save everything needed to connect in one tap. Profile name is optional and defaults to the host.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                        }

                        TextField {
                            id: nameField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Profile name (optional)")
                            text: page.draftName
                            Accessible.name: qsTr("Profile name")
                            onTextEdited: page.draftName = text
                        }

                        TextField {
                            id: hostField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Host (e.g. reflector.145500.xyz)")
                            text: page.draftHost
                            inputMethodHints: Qt.ImhNoPredictiveText
                            Accessible.name: qsTr("Server host")
                            onTextEdited: page.draftHost = text
                        }

                        TextField {
                            id: portField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Port")
                            text: page.draftPort
                            validator: IntValidator { bottom: 1; top: 65535 }
                            inputMethodHints: Qt.ImhDigitsOnly
                            Accessible.name: qsTr("Server port")
                            onTextEdited: page.draftPort = text
                        }

                        TextField {
                            id: callsignField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Callsign")
                            text: page.draftCallsign
                            inputMethodHints: Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText
                            Accessible.name: qsTr("Callsign")
                            onTextEdited: page.draftCallsign = text
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: authKeyField

                                Layout.fillWidth: true
                                placeholderText: qsTr("Authentication Key")
                                text: page.draftAuthKey
                                echoMode: showKeyBox.checked ? TextInput.Normal : TextInput.Password
                                Accessible.name: qsTr("Authentication key")
                                onTextEdited: page.draftAuthKey = text
                            }

                            CheckBox {
                                id: showKeyBox

                                text: qsTr("Show")
                                Accessible.name: qsTr("Show authentication key")
                            }
                        }

                        TextField {
                            id: talkgroupField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Talkgroup")
                            text: page.draftTalkgroup
                            validator: IntValidator { bottom: 0 }
                            inputMethodHints: Qt.ImhDigitsOnly
                            Accessible.name: qsTr("Talkgroup")
                            onTextEdited: page.draftTalkgroup = text
                        }

                        TextField {
                            id: monitoredTalkgroupsField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Monitored Talkgroups (e.g. 112++,240,2403+)")
                            text: page.draftMonitoredTalkgroups
                            inputMethodHints: Qt.ImhNoPredictiveText
                            Accessible.name: qsTr("Monitored talkgroups")
                            onTextEdited: page.draftMonitoredTalkgroups = text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Optional. Use svxlink syntax with commas and + priority markers. Example: 112++,240,2403+")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                        }

                        TextField {
                            id: tgSelectTimeoutField

                            Layout.fillWidth: true
                            placeholderText: qsTr("Talkgroup Idle Timeout (s)")
                            text: page.draftTgSelectTimeout
                            validator: IntValidator { bottom: 1 }
                            inputMethodHints: Qt.ImhDigitsOnly
                            Accessible.name: qsTr("Talkgroup idle timeout seconds")
                            onTextEdited: page.draftTgSelectTimeout = text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("How long the selected talkgroup stays active after the last local or remote activity. When it expires, the app returns to monitor mode (TG 0). SVXLink TG_SELECT_TIMEOUT. Default: 30 seconds.")
                            wrapMode: Text.WordWrap
                            color: "#556070"
                        }
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: page.editIndex >= 0 ? qsTr("Save Changes") : qsTr("Save Profile")
            enabled: page.formComplete
            Accessible.name: page.editIndex >= 0 ? qsTr("Save profile changes") : qsTr("Save profile")

            onClicked: page.requestSave()
        }
    }
}
