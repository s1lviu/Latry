import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ProfileUtils.js" as ProfileUtils

Frame {
    id: card

    padding: compact ? 10 : 14
    implicitHeight: implicitContentHeight + topPadding + bottomPadding

    required property int index
    required property string profileId
    required property string name
    required property string host
    required property int port
    required property string callsign
    required property string authKey
    required property int talkgroup
    required property string monitoredTalkgroups
    required property int tgSelectTimeout
    required property bool isSelected
    required property bool connected
    required property bool canUse
    required property bool canDelete
    required property color tintedSurfaceColor
    required property color accentColor
    property bool compact: false
    readonly property string displayName: card.name.length > 0 ? card.name : card.host

    signal activateRequested(string profileId)
    signal editRequested(int editIndex, var draftProfile)
    signal deleteRequested(int editIndex)

    Accessible.role: Accessible.Grouping
    Accessible.name: isSelected
                     ? qsTr("Selected profile %1").arg(card.displayName)
                     : qsTr("Profile %1").arg(card.displayName)
    Accessible.description: qsTr("%1. Callsign %2. Default talkgroup %3.")
                            .arg(card.host + ":" + card.port)
                            .arg(card.callsign)
                            .arg(card.talkgroup)
                            + qsTr(" Returns to monitor mode after %1 idle seconds.").arg(card.tgSelectTimeout)

    background: Rectangle {
        Accessible.ignored: true
        radius: card.compact ? 16 : 18
        color: card.isSelected ? card.tintedSurfaceColor : "#f8fafc"
        border.color: card.isSelected ? card.accentColor : "#d7deee"
    }

    contentItem: ColumnLayout {
        spacing: card.compact ? 6 : 8

        Rectangle {
            id: activationSurface

            Layout.fillWidth: true
            radius: card.compact ? 12 : 14
            color: activationArea.pressed
                   ? (card.isSelected ? "#dce6ff" : "#eef4ff")
                   : "transparent"
            border.color: (!card.isSelected && card.canUse) ? "#dbe6ff" : "transparent"
            border.width: 1
            implicitHeight: activationColumn.implicitHeight + (card.compact ? 10 : 12)
            Accessible.role: Accessible.Button
            Accessible.name: card.isSelected
                             ? qsTr("Selected profile %1").arg(card.displayName)
                             : qsTr("Activate profile %1").arg(card.displayName)
            Accessible.description: qsTr("%1. Callsign %2. Default talkgroup %3.")
                                    .arg(card.host + ":" + card.port)
                                    .arg(card.callsign)
                                    .arg(card.talkgroup)
            Accessible.focusable: activationArea.enabled
            Accessible.onPressAction: {
                if (activationArea.enabled)
                    card.activateRequested(card.profileId)
            }

            ColumnLayout {
                id: activationColumn

                anchors.fill: parent
                anchors.margins: card.compact ? 5 : 6
                spacing: card.compact ? 6 : 8

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: card.displayName
                            font.pixelSize: card.compact ? 16 : 18
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        Label {
                            Layout.fillWidth: true
                            text: card.host + ":" + card.port
                            wrapMode: Text.WordWrap
                            color: "#334155"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                    }

                    Rectangle {
                        visible: card.isSelected
                        radius: 999
                        color: "#dce6ff"
                        implicitWidth: selectedChip.implicitWidth + (card.compact ? 14 : 18)
                        implicitHeight: selectedChip.implicitHeight + (card.compact ? 8 : 10)
                        Accessible.ignored: true

                        Label {
                            id: selectedChip

                            anchors.centerIn: parent
                            text: card.connected ? qsTr("Active") : qsTr("Selected")
                            color: "#173b8f"
                            font.bold: true
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Callsign %1 | Default TG %2 | Idle timeout %3s")
                          .arg(card.callsign)
                          .arg(card.talkgroup)
                          .arg(card.tgSelectTimeout)
                    wrapMode: Text.WordWrap
                    color: "#556070"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Label {
                    objectName: "monitoredTalkgroupsLabel"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: ProfileUtils.monitoredTalkgroupsSummary(card.monitoredTalkgroups)
                          ? qsTr("Monitors %1").arg(ProfileUtils.monitoredTalkgroupsSummary(card.monitoredTalkgroups))
                          : ""
                    wrapMode: Text.WordWrap
                    color: "#556070"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Label {
                    Layout.fillWidth: true
                    text: card.isSelected
                          ? (card.connected
                             ? qsTr("Active for the current session")
                             : qsTr("Selected for the next connection"))
                          : (card.canUse
                             ? (card.connected
                                ? qsTr("Tap to reconnect using this profile")
                                : qsTr("Tap to make this the active profile"))
                             : qsTr("Stop transmitting or wait for the current switch to finish to activate this profile"))
                    wrapMode: Text.WordWrap
                    color: card.isSelected ? "#173b8f" : "#334155"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            MouseArea {
                id: activationArea

                objectName: "activationArea"
                anchors.fill: parent
                enabled: !card.isSelected && card.canUse
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                Accessible.ignored: true
                onClicked: card.activateRequested(card.profileId)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item {
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            Button {
                objectName: "editButton"
                text: qsTr("Edit")
                flat: true
                Accessible.name: qsTr("Edit profile %1").arg(card.displayName)
                onClicked: {
                    card.editRequested(card.index, {
                        profileId: card.profileId,
                        name: card.name,
                        host: card.host,
                        port: card.port,
                        callsign: card.callsign,
                        authKey: card.authKey,
                        talkgroup: card.talkgroup,
                        monitoredTalkgroups: card.monitoredTalkgroups,
                        tgSelectTimeout: card.tgSelectTimeout
                    })
                }
            }

            Button {
                objectName: "deleteButton"
                text: qsTr("Delete")
                flat: true
                enabled: card.canDelete
                Accessible.name: qsTr("Delete profile %1").arg(card.displayName)
                onClicked: card.deleteRequested(card.index)
            }
        }
    }
}
