pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    required property string profileName
    required property bool connected
    required property int defaultTalkgroup
    required property int currentTalkgroup
    required property var availableTalkgroups

    signal talkgroupChosen(int talkgroup, bool persistDefault)

    modal: true
    title: qsTr("Select Talkgroup")
    padding: 20
    width: Math.min(parent ? parent.width - 24 : 360, 420)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.max(12, Math.round((parent.height - height) / 2)) : 0
    standardButtons: Dialog.NoButton

    function talkgroupTitle(talkgroup) {
        return talkgroup === 0 ? qsTr("Monitor Mode") : qsTr("TG %1").arg(talkgroup)
    }

    function quickLabel(talkgroup) {
        const tags = []

        if (talkgroup === dialog.currentTalkgroup)
            tags.push(qsTr("Current"))
        if (talkgroup === dialog.defaultTalkgroup)
            tags.push(qsTr("Default"))

        return tags.length > 0
                ? qsTr("%1\n%2").arg(dialog.talkgroupTitle(talkgroup)).arg(tags.join(" + "))
                : dialog.talkgroupTitle(talkgroup)
    }

    function submitTalkgroup(value, persistDefault) {
        const trimmed = String(value).trim()
        if (trimmed.length === 0)
            return

        const parsed = Number(trimmed)
        if (!Number.isFinite(parsed) || parsed < 0)
            return

        dialog.talkgroupChosen(parsed, persistDefault)
        dialog.close()
    }

    onOpened: {
        customTalkgroupField.text = ""
        persistDefaultCheck.checked = false
    }

    contentItem: ColumnLayout {
        width: dialog.width - dialog.leftPadding - dialog.rightPadding
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: dialog.connected
                  ? qsTr("Switch the live session talkgroup for %1. Save it as the profile default only when you want future reconnects to start there.")
                      .arg(dialog.profileName || qsTr("this profile"))
                  : qsTr("Choose the default talkgroup for %1 before connecting.")
                      .arg(dialog.profileName || qsTr("this profile"))
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 340 ? 3 : 2
            rowSpacing: 8
            columnSpacing: 8

            Repeater {
                model: dialog.availableTalkgroups.length

                delegate: Button {
                    required property int index

                    readonly property int talkgroupValue: Number(dialog.availableTalkgroups[index])

                    Layout.fillWidth: true
                    text: dialog.quickLabel(talkgroupValue)
                    onClicked: dialog.submitTalkgroup(talkgroupValue, dialog.connected ? persistDefaultCheck.checked : true)
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: 14
            background: Rectangle {
                radius: 18
                color: "#f8fafc"
                border.color: "#d7deee"
            }

            contentItem: ColumnLayout {
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Custom Talkgroup")
                    font.pixelSize: 16
                    font.bold: true
                }

                TextField {
                    id: customTalkgroupField

                    Layout.fillWidth: true
                    placeholderText: qsTr("Enter TG number")
                    inputMethodHints: Qt.ImhDigitsOnly
                }

                CheckBox {
                    id: persistDefaultCheck

                    visible: dialog.connected
                    text: qsTr("Save as this profile's default TG")
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: dialog.connected ? qsTr("Switch TG") : qsTr("Save Default TG")
                        enabled: customTalkgroupField.text.trim().length > 0
                        onClicked: dialog.submitTalkgroup(customTalkgroupField.text, dialog.connected ? persistDefaultCheck.checked : true)
                    }
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    }
}
