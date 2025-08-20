import QtQuick
import QtQuick.Controls

Dialog {
    id: root
    title: "Keep Latry alive in background"
    modal: true
    
    // Make dialog full-width and responsive
    width: parent.width - 40  // Full width with 20px margin on each side
    height: Math.min(parent.height * 0.85, 800)  // Take most of screen height
    anchors.centerIn: parent

    property alias instructionsText: instructionsLabel.text

    header: Rectangle {
        color: "#f0f0f0"
        height: 60
        width: parent.width
        
        Label {
            text: root.title
            font.bold: true
            font.pixelSize: 18
            anchors.centerIn: parent
            color: "#333333"
        }
    }

    contentItem: Rectangle {
        color: "white"
        border.color: "#e0e0e0"
        border.width: 1
        
        ScrollView {
            id: scrollView
            anchors.fill: parent
            anchors.margins: 10
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Label {
                id: instructionsLabel
                width: scrollView.availableWidth
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                font.pixelSize: 14
                lineHeight: 1.4
                color: "#333333"
                
                // Handle HTML content properly
                onLinkActivated: function(link) {
                    Qt.openUrlExternally(link)
                }
            }
        }
    }

    footer: Rectangle {
        color: "#f8f8f8"
        height: 60
        width: parent.width
        border.color: "#e0e0e0"
        border.width: 1
        
        Button {
            text: "Got it"
            anchors.centerIn: parent
            onClicked: root.close()
            
            background: Rectangle {
                color: parent.pressed ? "#0066cc" : "#007fff"
                radius: 6
                border.color: "#0066cc"
                border.width: 1
            }
            
            contentItem: Text {
                text: parent.text
                color: "white"
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
