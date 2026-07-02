import QtQuick
import QtQuick.Controls
import QtTest

import "../.." as App

TestCase {
    id: testCase

    name: "HomePage"
    when: windowShown

    property bool tapToTalkButtonVisible: true

    QtObject {
        id: reflectorClientStub
        property string connectionStatus: "Connected to test"
        property bool isDisconnected: false
        property bool pttActive: false
        property bool isReceivingAudio: false
        property string currentTalker: ""
        property string currentTalkerName: ""
        property bool audioReady: true
        property string txTimeString: "00:05"
        property bool txTimeoutWarning: false
        property bool liveTranscriptionEnabled: true
        property string transcriptionText: "CQ test"
        property real rxMeterLevel: 0.2
        property real rxMeterPeakLevel: 0.3
        property real txMeterLevel: 0.1
        property real txMeterPeakLevel: 0.2
        property string softwareVersion: "0.0.17"
    }

    Component {
        id: regularHomeComponent

        App.HomePage {
            width: 400
            height: 900
            uiMetrics: App.UiMetrics {
                windowWidth: 400
                windowHeight: 900
            }
            contentPadding: 12
            safeAreaTop: 0
            safeAreaLeft: 0
            safeAreaRight: 0
            safeAreaBottom: 0
            surfaceColor: "white"
            borderColor: "#d7deee"
            selectedProfile: {
                "name": "Primary",
                "talkgroup": 9,
                "callsign": "YO6SAY-APX"
            }
            profilesCount: 1
            selectedProfileEndpoint: "example.org:5300"
            selectedProfileMeta: "YO6SAY-APX"
            canConnect: true
            currentTalkgroup: 9
            canSwitchProfile: true
            canSwitchTalkgroup: true
            profileSwitchInProgress: false
            pendingProfileName: ""
            rxMeterLevel: reflectorClientStub.rxMeterLevel
            rxMeterPeakLevel: reflectorClientStub.rxMeterPeakLevel
            txMeterLevel: reflectorClientStub.txMeterLevel
            txMeterPeakLevel: reflectorClientStub.txMeterPeakLevel
            reflectorClient: reflectorClientStub
            tapToTalkButtonVisible: testCase.tapToTalkButtonVisible
        }
    }

    Component {
        id: compactHomeComponent

        App.HomePage {
            width: 240
            height: 320
            uiMetrics: App.UiMetrics {
                windowWidth: 240
                windowHeight: 320
            }
            contentPadding: 8
            safeAreaTop: 0
            safeAreaLeft: 0
            safeAreaRight: 0
            safeAreaBottom: 0
            surfaceColor: "white"
            borderColor: "#d7deee"
            selectedProfile: {
                "name": "Primary",
                "talkgroup": 9,
                "callsign": "YO6SAY-APX"
            }
            profilesCount: 1
            selectedProfileEndpoint: "example.org:5300"
            selectedProfileMeta: "YO6SAY-APX"
            canConnect: true
            currentTalkgroup: 9
            canSwitchProfile: true
            canSwitchTalkgroup: true
            profileSwitchInProgress: false
            pendingProfileName: ""
            rxMeterLevel: reflectorClientStub.rxMeterLevel
            rxMeterPeakLevel: reflectorClientStub.rxMeterPeakLevel
            txMeterLevel: reflectorClientStub.txMeterLevel
            txMeterPeakLevel: reflectorClientStub.txMeterPeakLevel
            reflectorClient: reflectorClientStub
            tapToTalkButtonVisible: testCase.tapToTalkButtonVisible
        }
    }

    Component {
        id: signalSpy

        SignalSpy { }
    }

    function test_talkgroupLabel_formatsMonitorModeAndTalkgroups() {
        const page = createTemporaryObject(regularHomeComponent, this)
        verify(page !== null)

        compare(page.talkgroupLabel(0), "Monitor Mode")
        compare(page.talkgroupLabel(91), "TG 91")
    }

    function test_regularScreen_keepsLiveTranscriptionVisible() {
        const page = createTemporaryObject(regularHomeComponent, this)
        verify(page !== null)

        verify(!page.compactMode)
        compare(page.liveTranscriptionVisible, true)
    }

    function test_compactScreen_suppressesLiveTranscription() {
        const page = createTemporaryObject(compactHomeComponent, this)
        verify(page !== null)

        verify(page.compactMode)
        compare(page.liveTranscriptionVisible, false)
    }

    function test_scrollBar_followsAndroidAccessibilityWorkaround() {
        const page = createTemporaryObject(regularHomeComponent, this)
        verify(page !== null)

        const verticalScrollBar = findChild(page, "homeVerticalScrollBar")
        verify(verticalScrollBar !== null)
        compare(verticalScrollBar.Accessible.ignored, page.androidRangeAccessibilityWorkaround)
    }

    function test_shutdownButton_emitsShutdownRequested() {
        const page = createTemporaryObject(regularHomeComponent, this)
        verify(page !== null)
        wait(0)

        const spy = signalSpy.createObject(page, {
            target: page,
            signalName: "shutdownRequested"
        })
        verify(spy !== null)
        verify(spy.valid)

        const shutdownButton = findChild(page, "shutdownButton")
        verify(shutdownButton !== null)
        const shutdownButtonCenter = shutdownButton.mapToItem(page,
                                                              shutdownButton.width / 2,
                                                              shutdownButton.height / 2)
        compare(shutdownButtonCenter.x, page.width / 2)

        shutdownButton.clicked()
        compare(spy.count, 1)
    }

    function test_liveSessionProfile_showsProfileNameAndCallsign() {
        const page = createTemporaryObject(regularHomeComponent, this)
        verify(page !== null)
        wait(0)

        const nameLabel = findChild(page, "liveSessionProfileNameLabel")
        const callsignLabel = findChild(page, "liveSessionProfileCallsignLabel")
        verify(nameLabel !== null)
        verify(callsignLabel !== null)

        compare(page.selectedProfileName, "Primary")
        compare(page.selectedProfileCallsign, "YO6SAY-APX")
        compare(nameLabel.text, "Primary")
        compare(callsignLabel.text, "YO6SAY-APX")
    }

    function cleanup() {
        reflectorClientStub.pttActive = false
        reflectorClientStub.isReceivingAudio = false
        reflectorClientStub.currentTalker = ""
        testCase.tapToTalkButtonVisible = true
    }

    function createStandaloneCompactHomePage() {
        const page = compactHomeComponent.createObject(null)
        verify(page !== null)
        page.visible = true
        wait(0)
        return page
    }

    function test_compactScreen_keepsTouchPttButtonVisibleWhileTransmittingWhenSettingEnabled() {
        reflectorClientStub.pttActive = true

        const page = createStandaloneCompactHomePage()

        const pttButton = findChild(page, "pttButton")
        const txStatusLabel = findChild(page, "txStatusLabel")
        verify(pttButton !== null)
        verify(txStatusLabel !== null)

        compare(page.tapToTalkButtonVisible, true)
        compare(pttButton.visible, true)
        compare(txStatusLabel.text, "TX Time: 00:05")

        page.destroy()
    }

    function test_compactScreen_keepsTouchPttButtonForOnScreenTxAndUsesShorterHeight() {
        reflectorClientStub.pttActive = true

        const page = createTemporaryObject(compactHomeComponent, this)
        verify(page !== null)
        wait(0)

        const pttButton = findChild(page, "pttButton")
        verify(pttButton !== null)

        compare(pttButton.text, "TX - TAP TO STOP")
        compare(page.compactPttButtonHeight, 56)
    }

    function test_tapToTalkButtonVisibilitySettingHidesButton() {
        testCase.tapToTalkButtonVisible = false

        const page = createStandaloneCompactHomePage()

        const pttButton = findChild(page, "pttButton")
        verify(pttButton !== null)

        compare(pttButton.visible, false)

        page.destroy()
    }
}
