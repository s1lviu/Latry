pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property string labelText
    required property real level
    required property real peakLevel
    required property bool active
    property color accentColor: "#2563eb"
    property bool compact: false
    readonly property int segmentCount: 16
    readonly property real segmentGap: 2

    function clampUnit(value) {
        const numericValue = Number(value)
        if (Number.isNaN(numericValue))
            return 0.0
        return Math.max(0.0, Math.min(1.0, numericValue))
    }

    readonly property real normalizedLevel: clampUnit(level)
    readonly property real normalizedPeakLevel: Math.max(normalizedLevel, clampUnit(peakLevel))
    readonly property int litSegmentCount: normalizedLevel > 0.0 ? Math.max(1, Math.ceil(normalizedLevel * segmentCount)) : 0
    readonly property int peakSegmentIndex: normalizedPeakLevel > 0.0
                                            ? Math.max(0, Math.min(segmentCount - 1, Math.ceil(normalizedPeakLevel * segmentCount) - 1))
                                            : -1

    function activeSegmentColor(index) {
        const ratio = (index + 1) / segmentCount
        if (ratio >= 0.88)
            return "#dc2626"
        if (ratio >= 0.68)
            return "#f59e0b"
        return "#22c55e"
    }

    function inactiveSegmentColor(index) {
        const ratio = (index + 1) / segmentCount
        if (ratio >= 0.88)
            return "#fca5a5"
        if (ratio >= 0.68)
            return "#fde68a"
        return "#bbf7d0"
    }

    implicitWidth: compact ? 220 : 240
    implicitHeight: compact ? 24 : 28

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("%1 audio level").arg(labelText)
    Accessible.description: active ? qsTr("%1 meter is active").arg(labelText) : qsTr("%1 meter is idle").arg(labelText)

    RowLayout {
        anchors.fill: parent
        spacing: root.compact ? 6 : 8
        // Prevent per-frame accessibility invalidation: the 16 meter segments
        // change color/opacity on every decoded audio frame (~20 ms).  Each
        // change triggers invalidateVirtualViewId → BlockingQueuedConnection
        // to the Qt thread, which is busy processing the next frame → ANR.
        // The root Item already exposes Accessible.name / .description for
        // TalkBack, so ignoring these rapidly-updating children is safe.
        Accessible.ignored: true

        Rectangle {
            Layout.preferredWidth: root.compact ? 34 : 38
            Layout.preferredHeight: root.compact ? 20 : 22
            radius: root.compact ? 10 : 11
            color: root.active ? root.accentColor : "#e2e8f0"
            border.color: root.active ? root.accentColor : "#cbd5e1"

            Label {
                anchors.centerIn: parent
                text: root.labelText
                font.bold: true
                font.pixelSize: root.compact ? 11 : 12
                color: root.active ? "#ffffff" : "#475569"
            }
        }

        Item {
            id: meterTrack

            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 16 : 18
            readonly property real segmentWidth: Math.max(4, (width - (root.segmentGap * (root.segmentCount - 1))) / root.segmentCount)

            Rectangle {
                anchors.fill: parent
                radius: 9
                color: "#e5ebf5"
                border.color: "#d0d9e8"
            }

            Repeater {
                model: root.segmentCount

                Rectangle {
                    required property int index
                    readonly property bool filled: index < root.litSegmentCount
                    readonly property bool peak: index === root.peakSegmentIndex
                    x: Math.round(index * (meterTrack.segmentWidth + root.segmentGap))
                    y: 2
                    width: meterTrack.segmentWidth
                    height: meterTrack.height - 4
                    radius: 4
                    color: filled ? root.activeSegmentColor(index) : root.inactiveSegmentColor(index)
                    opacity: filled ? 1.0 : 0.22
                    border.width: peak ? 2 : 0
                    border.color: peak ? "#f8fafc" : "transparent"
                }
            }
        }

        Label {
            Layout.preferredWidth: root.compact ? 26 : 30
            horizontalAlignment: Text.AlignRight
            text: root.active || root.normalizedLevel > 0.02 ? qsTr("Live") : qsTr("Idle")
            color: root.active ? root.accentColor : "#64748b"
            font.pixelSize: root.compact ? 10 : 11
            font.bold: true
        }
    }
}
