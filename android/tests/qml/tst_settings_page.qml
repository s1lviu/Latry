import QtQuick
import QtQuick.Controls
import QtTest

import "../.." as App

TestCase {
    id: testCase

    name: "SettingsPage"
    when: windowShown

    property bool tapToTalkButtonVisible: true

    QtObject {
        id: reflectorClientStub
        property bool isDisconnected: true
        property bool pttActive: false
        property bool hardwarePttEnabled: false
        property int learnedHardwarePttKeyCode: -1
        property bool hardwarePttLearningActive: false
        property int hardwarePttLearningResult: 0
        property bool liveTranscriptionEnabled: false
        property bool transcriptionAvailable: false
        property var transcriptionInstalledLanguages: []
        property var transcriptionDownloadableLanguages: []
        property bool transcriptionModelDownloadAvailable: false
        property bool transcriptionModelDownloadInProgress: false
        property int transcriptionModelDownloadProgress: -1
        property string transcriptionModelDownloadStatus: ""
        signal hardwarePttSettingsChanged()
    }

    Component {
        id: settingsComponent

        App.SettingsPage {
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
            tintedSurfaceColor: "#edf2ff"
            borderColor: "#d7deee"
            accentColor: "#2c5cff"
            profilesModel: ListModel {}
            nodeInfoReadOnlyEntries: []
            nodeInfoReservedKeys: ["sw", "swVer", "tip", "Website", "callsign"]
            nodeInfoPropertiesModel: ListModel {}
            selectedProfileId: ""
            profileSwitchInProgress: false
            availableAudioRoutes: [
                { id: "speaker", name: "Speaker" },
                { id: "wired_headset", name: "Wired Headset" },
                { id: "bluetooth", name: "Bluetooth" }
            ]
            currentAudioRoute: "speaker"
            preferredAudioRoute: "speaker"
            rxAudioLevelDb: 0
            txAudioLevelDb: 0
            txTimeoutSeconds: 175
            pttHangTimeMs: 100
            tapToTalkButtonVisible: testCase.tapToTalkButtonVisible
            reflectorClient: reflectorClientStub
        }
    }

    Component {
        id: compactSettingsComponent

        App.SettingsPage {
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
            tintedSurfaceColor: "#edf2ff"
            borderColor: "#d7deee"
            accentColor: "#2c5cff"
            profilesModel: ListModel {}
            nodeInfoReadOnlyEntries: []
            nodeInfoReservedKeys: ["sw", "swVer", "tip", "Website", "callsign"]
            nodeInfoPropertiesModel: ListModel {}
            selectedProfileId: ""
            profileSwitchInProgress: false
            availableAudioRoutes: [
                { id: "speaker", name: "Speaker" },
                { id: "wired_headset", name: "Wired Headset" },
                { id: "bluetooth", name: "Bluetooth" }
            ]
            currentAudioRoute: "speaker"
            preferredAudioRoute: "speaker"
            rxAudioLevelDb: 0
            txAudioLevelDb: 0
            txTimeoutSeconds: 175
            pttHangTimeMs: 100
            tapToTalkButtonVisible: testCase.tapToTalkButtonVisible
            reflectorClient: reflectorClientStub
        }
    }

    function cleanup() {
        testCase.tapToTalkButtonVisible = true
    }

    function test_audioRouteHelpers_normalizeExpectedText() {
        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        compare(page.audioRouteName("bluetooth"), "Bluetooth")
        compare(page.audioRouteName("speaker"), "Speaker")
        compare(page.audioRouteName("unknown"), "Speaker")
        verify(page.audioRouteAvailable("wired_headset"))
        verify(!page.audioRouteAvailable("car"))
        compare(page.audioRouteDescription("wired_headset"), "Wired audio route")
        compare(page.audioRouteDescription("bluetooth"), "Bluetooth voice route")
        compare(page.audioRouteDescription("speaker"), "Speaker + Internal mic")
    }

    function test_audioLevelHelpers_formatExpectedLabels() {
        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        verify(page.isZeroLevel(0.01))
        verify(!page.isZeroLevel(0.2))
        compare(page.signedDbText(2.5, 1), "+2.5 dB")
        compare(page.signedDbText(-3, 0), "-3 dB")
        compare(page.rxAudioLevelText(0.0), "Off")
        compare(page.rxAudioLevelText(2.5), "+2.5 dB")
        compare(page.txAudioLevelText(0.0), "Normal")
        compare(page.txAudioLevelText(4.0), "+4 dB")

        page.rxAudioLevelDb = 1.5
        page.txAudioLevelDb = 0
        verify(page.hasCustomAudioLevels())
    }

    function test_reservedNodeInfoKey_checkIsCaseInsensitive() {
        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        verify(page.isReservedNodeInfoKey("Website"))
        verify(page.isReservedNodeInfoKey(" callsign "))
        verify(!page.isReservedNodeInfoKey("rig"))
        verify(!page.isReservedNodeInfoKey(""))
    }

    function test_compactMode_switchesToSectionChooserAndDropsTranscription() {
        reflectorClientStub.transcriptionDownloadableLanguages = [
            { name: "English", tag: "en-US", status: "Ready" }
        ]
        reflectorClientStub.transcriptionModelDownloadAvailable = true

        const page = createTemporaryObject(compactSettingsComponent, this)
        verify(page !== null)

        verify(page.compactSettingsMode)
        verify(page.compactSectionChooserVisible)
        compare(page.liveTranscriptionSettingsVisible, false)

        page.compactSection = "audio"
        compare(page.compactSectionChooserVisible, false)
        compare(page.compactSectionTitle(page.compactSection), "Audio")

        reflectorClientStub.transcriptionDownloadableLanguages = []
        reflectorClientStub.transcriptionModelDownloadAvailable = false
    }

    function test_largeScreen_keepsTranscriptionPolicyEnabledWhenModelsExist() {
        reflectorClientStub.transcriptionInstalledLanguages = [
            { name: "English", tag: "en-US", status: "Installed" }
        ]

        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        verify(!page.compactSettingsMode)
        compare(page.uiMetrics.liveTranscriptionAllowed, true)
        compare(page.liveTranscriptionSettingsVisible, Qt.platform.os === "android")

        reflectorClientStub.transcriptionInstalledLanguages = []
    }

    function test_hardwarePttField_tracksReflectorSettings() {
        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        const keyCodeField = findChild(page, "hardwarePttKeyCodeField")
        verify(keyCodeField !== null)
        compare(keyCodeField.text, "")

        reflectorClientStub.learnedHardwarePttKeyCode = 88
        reflectorClientStub.hardwarePttSettingsChanged()
        compare(keyCodeField.text, "88")

        reflectorClientStub.learnedHardwarePttKeyCode = -1
        reflectorClientStub.hardwarePttSettingsChanged()
        compare(keyCodeField.text, "")
    }

    function test_tapToTalkButtonSwitchTracksSettingAndEmitsRequest() {
        testCase.tapToTalkButtonVisible = false

        const page = createTemporaryObject(settingsComponent, this)
        verify(page !== null)

        const visibilitySwitch = findChild(page, "tapToTalkButtonVisibleSwitch")
        verify(visibilitySwitch !== null)
        compare(visibilitySwitch.checked, false)

        let requestedVisible = null
        page.tapToTalkButtonVisibleRequested.connect(function(visible) {
            requestedVisible = visible
        })

        visibilitySwitch.checked = true
        visibilitySwitch.clicked()
        compare(requestedVisible, true)

        testCase.tapToTalkButtonVisible = true
        wait(0)
        compare(visibilitySwitch.checked, true)
    }

    function test_compactHardwarePttControls_stackVertically() {
        reflectorClientStub.learnedHardwarePttKeyCode = 261

        const page = createTemporaryObject(compactSettingsComponent, this)
        verify(page !== null)

        page.compactSection = "device"
        wait(0)

        const learnButton = findChild(page, "learnPttButton")
        const clearButton = findChild(page, "clearHardwarePttKeyCodeButton")
        const manualLabel = findChild(page, "hardwarePttManualKeyLabel")
        const keyCodeField = findChild(page, "hardwarePttKeyCodeField")

        verify(learnButton !== null)
        verify(clearButton !== null)
        verify(manualLabel !== null)
        verify(keyCodeField !== null)
        compare(learnButton.text, "Learn")
        verify(clearButton.y > learnButton.y)
        verify(keyCodeField.y > manualLabel.y)

        reflectorClientStub.learnedHardwarePttKeyCode = -1
    }
}
