pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog

    required property var profilesModel
    required property string selectedProfileId
    required property bool connected
    required property bool busy
    required property string busyProfileId

    signal profileChosen(string profileId)

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    leftPadding: 18
    rightPadding: 18
    topPadding: 12
    bottomPadding: 18
    width: Math.min(parent ? parent.width - 16 : 420, 520)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? parent.height - height - 8 : 0

    Overlay.modal: Rectangle {
        color: "#660f172a"
    }

    background: Rectangle {
        color: "#ffffff"
        radius: 28
        border.color: "#d7deee"
    }

    contentItem: ColumnLayout {
        width: dialog.width - dialog.leftPadding - dialog.rightPadding
        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 44
            implicitHeight: 5
            radius: 999
            color: "#c8d3e5"
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: qsTr("Switch Profile")
                font.pixelSize: 24
                font.bold: true
            }

            Label {
                Layout.fillWidth: true
                text: dialog.connected
                      ? qsTr("Choose another server profile. The app will disconnect and reconnect using that server.")
                      : qsTr("Choose which saved server profile should be active on the main screen.")
                wrapMode: Text.WordWrap
                color: "#556070"
            }
        }

        ListView {
            id: profilesList

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, dialog.parent ? dialog.parent.height * 0.56 : 420)
            clip: true
            spacing: 10
            model: dialog.profilesModel

            delegate: ItemDelegate {
                id: delegateButton

                required property string profileId
                required property string name
                required property string host
                required property int port
                required property string callsign
                required property int talkgroup

                readonly property bool isCurrent: profileId === dialog.selectedProfileId
                readonly property bool isBusy: dialog.busy && profileId === dialog.busyProfileId

                width: ListView.view ? ListView.view.width : 0
                enabled: !isCurrent && !dialog.busy
                padding: 14

                Accessible.name: qsTr("%1. %2:%3. Callsign %4. Default talkgroup %5.")
                                 .arg(name)
                                 .arg(host)
                                 .arg(port)
                                 .arg(callsign)
                                 .arg(talkgroup)

                background: Rectangle {
                    radius: 20
                    color: delegateButton.down
                           ? "#e4ecff"
                           : delegateButton.isCurrent ? "#edf2ff" : "#f8fafc"
                    border.color: delegateButton.isCurrent ? "#2c5cff" : "#d7deee"
                    border.width: delegateButton.visualFocus ? 2 : 1
                }

                contentItem: ColumnLayout {
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                Layout.fillWidth: true
                                text: delegateButton.name
                                font.pixelSize: 18
                                font.bold: true
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("%1:%2").arg(delegateButton.host).arg(delegateButton.port)
                                color: "#334155"
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            visible: delegateButton.isCurrent
                            radius: 999
                            color: "#dce6ff"
                            implicitWidth: currentLabel.implicitWidth + 18
                            implicitHeight: currentLabel.implicitHeight + 10

                            Label {
                                id: currentLabel

                                anchors.centerIn: parent
                                text: qsTr("Current")
                                color: "#173b8f"
                                font.bold: true
                            }
                        }

                        BusyIndicator {
                            visible: delegateButton.isBusy
                            running: visible
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Callsign %1 | Default TG %2").arg(delegateButton.callsign).arg(delegateButton.talkgroup)
                        color: "#556070"
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: delegateButton.isCurrent
                              ? qsTr("Currently active on the main screen")
                              : (dialog.connected
                                 ? qsTr("Tap to reconnect using this profile")
                                 : qsTr("Tap to make this the active profile"))
                        color: delegateButton.isCurrent ? "#173b8f" : "#334155"
                        wrapMode: Text.WordWrap
                    }
                }

                onClicked: {
                    dialog.profileChosen(delegateButton.profileId)
                    dialog.close()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                visible: dialog.busy
                text: qsTr("Profile switch in progress")
                color: "#173b8f"
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Button {
                text: qsTr("Close")
                onClicked: dialog.close()
            }
        }
    }
}
