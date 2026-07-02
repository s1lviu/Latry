import QtQuick

QtObject {
    id: root

    required property real windowWidth
    required property real windowHeight

    readonly property real shortSide: Math.min(windowWidth, windowHeight)
    readonly property real longSide: Math.max(windowWidth, windowHeight)

    readonly property bool isNarrowScreen: shortSide < 250
    readonly property bool isCompactScreen: shortSide < 360
    readonly property bool isSinglePaneScreen: shortSide < 360 && longSide < 450

    readonly property int pagePadding: isSinglePaneScreen ? 8 : (isCompactScreen ? 12 : 14)
    readonly property int pageSpacing: isSinglePaneScreen ? 8 : 12
    readonly property int sectionPadding: isSinglePaneScreen ? 10 : 14
    readonly property int nestedSectionPadding: isSinglePaneScreen ? 10 : 12
    readonly property int frameRadius: isSinglePaneScreen ? 16 : 20
    readonly property int nestedFrameRadius: isSinglePaneScreen ? 14 : 16
    readonly property int titleFontSize: isSinglePaneScreen ? 22 : 28
    readonly property int pageTitleFontSize: isSinglePaneScreen ? 20 : 24
    readonly property int sectionTitleFontSize: isSinglePaneScreen ? 18 : 20
    readonly property int statusFontSize: isSinglePaneScreen ? 14 : 16
    readonly property int bodyFontSize: isSinglePaneScreen ? 13 : 14
    readonly property int captionFontSize: isSinglePaneScreen ? 11 : 12
    readonly property int footerFontSize: isSinglePaneScreen ? 9 : 10
    readonly property int pttButtonHeight: isSinglePaneScreen ? 64 : 72
    readonly property bool liveTranscriptionAllowed: !isSinglePaneScreen
}
