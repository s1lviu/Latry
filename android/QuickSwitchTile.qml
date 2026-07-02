import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    required property string labelText
    required property string primaryText
    property string secondaryText: ""
    property color accentColor: "#2c5cff"
    property bool emphasized: false
    property bool compact: false

    implicitHeight: tileLayout.implicitHeight + topPadding + bottomPadding
    leftPadding: compact ? 8 : 14
    rightPadding: compact ? 8 : 14
    topPadding: compact ? 8 : 14
    bottomPadding: compact ? 8 : 14

    Accessible.name: secondaryText.length > 0
                     ? qsTr("%1. %2. %3").arg(labelText).arg(primaryText).arg(secondaryText)
                     : qsTr("%1. %2").arg(labelText).arg(primaryText)

    background: Rectangle {
        radius: control.compact ? 16 : 18
        color: !control.enabled
               ? "#eef2f7"
               : control.down
                 ? "#dfe9ff"
                 : control.emphasized ? "#edf2ff" : "#f8fafc"
        border.color: control.emphasized ? control.accentColor : "#d7deee"
        border.width: control.activeFocus ? 2 : 1
    }

    contentItem: ColumnLayout {
        id: tileLayout

        spacing: control.compact ? 2 : 4

        Label {
            Layout.fillWidth: true
            text: control.labelText
            color: "#556070"
            font.pixelSize: control.compact ? 10 : 12
            wrapMode: control.compact ? Text.NoWrap : Text.WordWrap
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: control.primaryText
            font.pixelSize: control.compact ? 15 : 18
            font.bold: true
            wrapMode: control.compact ? Text.NoWrap : Text.WordWrap
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: control.secondaryText
            color: "#334155"
            font.pixelSize: control.compact ? 11 : 13
            wrapMode: control.compact ? Text.NoWrap : Text.WordWrap
            elide: Text.ElideRight
        }
    }
}
