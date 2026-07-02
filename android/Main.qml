pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCore
import SvxlinkReflector.Client 1.0
import "ProfileUtils.js" as ProfileUtils

Window {
    id: window

    width: 360
    height: 640
    visible: true
    title: qsTr("Latry")
    color: "#f4f7fb"

    property color accentColor: "#2c5cff"
    property color surfaceColor: "#ffffff"
    property color tintedSurfaceColor: "#edf2ff"
    property color borderColor: "#d7deee"
    readonly property int contentPadding: uiMetrics.pagePadding
    property real minRxAudioLevelDb: 0.0
    property real maxRxAudioLevelDb: 9.0
    property real minTxAudioLevelDb: -12.0
    property real maxTxAudioLevelDb: 12.0
    readonly property var nodeInfoReservedKeys: ["sw", "swVer", "tip", "Website", "callsign"]

    UiMetrics {
        id: uiMetrics
        windowWidth: window.width
        windowHeight: window.height
    }

    property string selectedProfileId: ""
    property var selectedProfile: null
    property int pendingDeleteIndex: -1
    property string pendingDeleteName: ""
    property string pendingProfileSwitchId: ""
    property string pendingProfileSwitchName: ""
    property bool pendingProfileReconnectStarted: false

    onClosing: close => {
        if (stackView.depth > 1) {
            close.accepted = false
            stackView.pop()
            return
        }

        if (Qt.platform.os === "android" && ReflectorClient.voipBackgroundServiceRunning())
            return

        Qt.quit()
    }

    onActiveChanged: {
        if (!active && ReflectorClient.pttActive)
            ReflectorClient.forcePttRelease()
    }

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive && ReflectorClient.pttActive)
                ReflectorClient.forcePttRelease()
        }
    }

    Connections {
        target: ReflectorClient

        function onConnectionStatusChanged() {
            if (window.pendingProfileSwitchId === "")
                return

            if (!window.pendingProfileReconnectStarted && ReflectorClient.isDisconnected) {
                const targetProfileId = window.pendingProfileSwitchId
                window.pendingProfileReconnectStarted = true
                window.selectProfile(targetProfileId)
                Qt.callLater(function() {
                    window.connectSelectedProfile()
                })
                return
            }

            if (window.pendingProfileReconnectStarted
                    && (!ReflectorClient.isDisconnected
                        || !ReflectorClient.connectionStatus.startsWith("Disconnected"))) {
                window.pendingProfileSwitchId = ""
                window.pendingProfileSwitchName = ""
                window.pendingProfileReconnectStarted = false
            }
        }

        function onLiveTranscriptionEnabledChanged() {
            saved.liveTranscriptionEnabled = ReflectorClient.liveTranscriptionEnabled
        }
    }

    Connections {
        target: uiMetrics

        function onLiveTranscriptionAllowedChanged() {
            if (!uiMetrics.liveTranscriptionAllowed && ReflectorClient.liveTranscriptionEnabled)
                window.updateLiveTranscriptionEnabled(false)
        }
    }

    BatteryOptimizationDialog {
        id: batteryDialog
    }

    Connections {
        target: BatteryOptimizationHandler

        function onShowBatteryOptimizationInstructions(instructions) {
            batteryDialog.instructionsText = instructions
            batteryDialog.open()
        }
    }

    Settings {
        id: saved

        property string host: ""
        property string port: ""
        property string callsign: ""
        property string key: ""
        property string talkgroup: ""
        property string profilesJson: "[]"
        property string selectedProfileId: ""
        property string preferredAudioRoute: ""
        property real rxAudioLevelDb: 0.0
        property real txAudioLevelDb: 0.0
        property int txTimeoutSeconds: 175
        property int pttHangTimeMs: 100
        property bool tapToTalkButtonVisible: true
        property bool liveTranscriptionEnabled: false
        property string nodeInfoPropertiesJson: "[]"
    }

    ListModel {
        id: profilesModel
    }

    ListModel {
        id: nodeInfoPropertiesModel
    }

    function profilesAsArray() {
        const items = []
        for (let i = 0; i < profilesModel.count; ++i)
            items.push(ProfileUtils.copyProfile(profilesModel.get(i)))
        return items
    }

    function nodeInfoPropertiesAsArray() {
        const items = []
        for (let i = 0; i < nodeInfoPropertiesModel.count; ++i) {
            const entry = nodeInfoPropertiesModel.get(i)
            items.push({
                key: entry.propertyKey,
                value: entry.propertyValue
            })
        }
        return items
    }

    function isReservedNodeInfoKey(key) {
        const normalizedKey = ProfileUtils.trimmedText(key).toLowerCase()
        if (normalizedKey === "")
            return false

        for (let i = 0; i < nodeInfoReservedKeys.length; ++i) {
            if (String(nodeInfoReservedKeys[i]).toLowerCase() === normalizedKey)
                return true
        }

        return false
    }

    function effectiveNodeInfoProperties() {
        const items = []
        const existingIndexByKey = {}

        for (let i = 0; i < nodeInfoPropertiesModel.count; ++i) {
            const entry = nodeInfoPropertiesModel.get(i)
            const key = ProfileUtils.trimmedText(entry.propertyKey)
            const value = ProfileUtils.trimmedText(entry.propertyValue)
            const normalizedKey = key.toLowerCase()
            if (key === "" || value === "" || isReservedNodeInfoKey(key))
                continue

            if (Object.prototype.hasOwnProperty.call(existingIndexByKey, normalizedKey)) {
                items[existingIndexByKey[normalizedKey]] = {
                    key: key,
                    value: value
                }
                continue
            }

            existingIndexByKey[normalizedKey] = items.length
            items.push({
                key: key,
                value: value
            })
        }

        return items
    }

    function rebuildNodeInfoPropertiesModel(entries) {
        nodeInfoPropertiesModel.clear()
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            nodeInfoPropertiesModel.append({
                propertyKey: entry && entry.key !== undefined ? String(entry.key) : "",
                propertyValue: entry && entry.value !== undefined ? String(entry.value) : ""
            })
        }
    }

    function persistNodeInfoProperties() {
        saved.nodeInfoPropertiesJson = JSON.stringify(nodeInfoPropertiesAsArray())
        ReflectorClient.setCustomNodeInfoEntries(effectiveNodeInfoProperties())
    }

    function loadNodeInfoProperties() {
        const loadedEntries = []
        const rawJson = ProfileUtils.trimmedText(saved.nodeInfoPropertiesJson)

        if (rawJson !== "") {
            try {
                const parsed = JSON.parse(rawJson)
                if (Array.isArray(parsed)) {
                    for (let i = 0; i < parsed.length; ++i) {
                        const entry = parsed[i]
                        if (!entry || typeof entry !== "object")
                            continue

                        loadedEntries.push({
                            key: entry.key !== undefined ? String(entry.key) : "",
                            value: entry.value !== undefined ? String(entry.value) : ""
                        })
                    }
                }
            } catch (error) {
                console.warn("Failed to parse saved node info properties", error)
            }
        }

        rebuildNodeInfoPropertiesModel(loadedEntries)
        persistNodeInfoProperties()
    }

    function addNodeInfoProperty() {
        nodeInfoPropertiesModel.append({
            propertyKey: "",
            propertyValue: ""
        })
        persistNodeInfoProperties()
    }

    function updateNodeInfoProperty(index, key, value) {
        if (index < 0 || index >= nodeInfoPropertiesModel.count)
            return

        nodeInfoPropertiesModel.set(index, {
            propertyKey: key,
            propertyValue: value
        })
        persistNodeInfoProperties()
    }

    function deleteNodeInfoProperty(index) {
        if (index < 0 || index >= nodeInfoPropertiesModel.count)
            return

        nodeInfoPropertiesModel.remove(index)
        persistNodeInfoProperties()
    }

    function findProfileIndexById(profileId) {
        for (let i = 0; i < profilesModel.count; ++i) {
            if (profilesModel.get(i).profileId === profileId)
                return i
        }

        return -1
    }

    function refreshSelectedProfile() {
        const selectedIndex = findProfileIndexById(selectedProfileId)
        selectedProfile = selectedIndex >= 0 ? ProfileUtils.copyProfile(profilesModel.get(selectedIndex)) : null
        persistLegacySelection()
    }

    function persistLegacySelection() {
        if (selectedProfile) {
            saved.host = selectedProfile.host
            saved.port = String(selectedProfile.port)
            saved.callsign = selectedProfile.callsign
            saved.key = selectedProfile.authKey
            saved.talkgroup = String(selectedProfile.talkgroup)
        } else {
            saved.host = ""
            saved.port = ""
            saved.callsign = ""
            saved.key = ""
            saved.talkgroup = ""
        }
    }

    function persistProfiles() {
        saved.profilesJson = JSON.stringify(profilesAsArray())
        saved.selectedProfileId = selectedProfileId
        persistLegacySelection()
    }

    function rebuildProfilesModel(profiles) {
        profilesModel.clear()
        for (let i = 0; i < profiles.length; ++i)
            profilesModel.append(ProfileUtils.copyProfile(profiles[i]))
    }

    function ensureValidSelection(preferredId) {
        let nextId = ProfileUtils.trimmedText(preferredId)

        if (findProfileIndexById(nextId) < 0)
            nextId = ProfileUtils.trimmedText(selectedProfileId)
        if (findProfileIndexById(nextId) < 0)
            nextId = ProfileUtils.trimmedText(saved.selectedProfileId)
        if (findProfileIndexById(nextId) < 0 && profilesModel.count > 0)
            nextId = profilesModel.get(0).profileId
        if (findProfileIndexById(nextId) < 0)
            nextId = ""

        if (nextId !== selectedProfileId)
            selectedProfileId = nextId
        else
            refreshSelectedProfile()
    }

    function migratedLegacyProfile() {
        if (ProfileUtils.trimmedText(saved.host) === ""
                || ProfileUtils.trimmedText(saved.callsign) === ""
                || ProfileUtils.trimmedText(saved.key) === ""
                || ProfileUtils.trimmedText(saved.talkgroup) === "") {
            return null
        }

        return ProfileUtils.normalizeProfile({
            profileId: "",
            name: ProfileUtils.trimmedText(saved.host),
            host: saved.host,
            port: saved.port,
            callsign: saved.callsign,
            authKey: saved.key,
            talkgroup: saved.talkgroup
        }, qsTr("Imported Server"))
    }

    function loadProfiles() {
        const loadedProfiles = []
        const rawJson = ProfileUtils.trimmedText(saved.profilesJson)

        if (rawJson !== "") {
            try {
                const parsed = JSON.parse(rawJson)
                if (Array.isArray(parsed)) {
                    for (let i = 0; i < parsed.length; ++i) {
                        const normalized = ProfileUtils.normalizeProfile(parsed[i], qsTr("Server %1").arg(i + 1))
                        if (normalized)
                            loadedProfiles.push(normalized)
                    }
                }
            } catch (error) {
                console.warn("Failed to parse saved profiles", error)
            }
        }

        if (loadedProfiles.length === 0) {
            const migrated = migratedLegacyProfile()
            if (migrated)
                loadedProfiles.push(migrated)
        }

        rebuildProfilesModel(loadedProfiles)
        ensureValidSelection(saved.selectedProfileId)
        persistProfiles()
    }

    function createProfileSeed() {
        return {
            profileId: "",
            name: "",
            host: "",
            port: 5300,
            callsign: selectedProfile ? selectedProfile.callsign : "",
            authKey: "",
            talkgroup: selectedProfile ? selectedProfile.talkgroup : 9,
            monitoredTalkgroups: selectedProfile ? selectedProfile.monitoredTalkgroups : "",
            tgSelectTimeout: selectedProfile ? selectedProfile.tgSelectTimeout : 30
        }
    }

    function upsertProfile(profileData, editIndex) {
        const normalized = ProfileUtils.normalizeProfile(profileData, qsTr("Server"))
        if (!normalized)
            return false

        if (editIndex >= 0 && editIndex < profilesModel.count) {
            normalized.profileId = profilesModel.get(editIndex).profileId
            profilesModel.set(editIndex, normalized)
        } else {
            profilesModel.append(normalized)
            if (ReflectorClient.isDisconnected || selectedProfileId === "")
                selectedProfileId = normalized.profileId
        }

        ensureValidSelection(selectedProfileId)
        persistProfiles()

        if (!ReflectorClient.isDisconnected && normalized.profileId === selectedProfileId)
            ReflectorClient.updateProfileConfiguration(normalized.talkgroup,
                                                      normalized.monitoredTalkgroups,
                                                      normalized.tgSelectTimeout)

        return true
    }

    function requestDeleteProfile(index) {
        if (index < 0 || index >= profilesModel.count)
            return

        pendingDeleteIndex = index
        pendingDeleteName = profilesModel.get(index).name
        deleteProfileDialog.open()
    }

    function deleteProfile(index) {
        if (index < 0 || index >= profilesModel.count)
            return

        const removedProfileId = profilesModel.get(index).profileId
        const fallbackId = removedProfileId === selectedProfileId && profilesModel.count > 1
                ? profilesModel.get(index === 0 ? 1 : index - 1).profileId
                : selectedProfileId

        profilesModel.remove(index)

        if (removedProfileId === selectedProfileId)
            selectedProfileId = fallbackId

        ensureValidSelection(selectedProfileId)
        persistProfiles()
    }

    function selectProfile(profileId) {
        if (ProfileUtils.trimmedText(profileId) === "")
            return
        if (!ReflectorClient.isDisconnected && profileId !== selectedProfileId)
            return

        selectedProfileId = profileId
        ensureValidSelection(profileId)
        persistProfiles()
    }

    function requestProfileActivation(profileId) {
        if (ProfileUtils.trimmedText(profileId) === "" || profileId === selectedProfileId)
            return
        if (pendingProfileSwitchId !== "")
            return

        const targetIndex = findProfileIndexById(profileId)
        if (targetIndex < 0)
            return

        if (ReflectorClient.isDisconnected) {
            selectProfile(profileId)
            return
        }

        pendingProfileSwitchId = profileId
        pendingProfileSwitchName = profilesModel.get(targetIndex).name
        pendingProfileReconnectStarted = false
        ReflectorClient.disconnectFromServer()
    }

    function updateProfileTalkgroup(profileId, talkgroup) {
        const profileIndex = findProfileIndexById(profileId)
        if (profileIndex < 0)
            return false

        const updatedProfile = ProfileUtils.copyProfile(profilesModel.get(profileIndex))
        updatedProfile.talkgroup = ProfileUtils.normalizeTalkgroup(talkgroup)
        profilesModel.set(profileIndex, updatedProfile)

        if (profileId === selectedProfileId)
            refreshSelectedProfile()

        if (profileId === selectedProfileId && !ReflectorClient.isDisconnected)
            ReflectorClient.updateProfileConfiguration(updatedProfile.talkgroup,
                                                      updatedProfile.monitoredTalkgroups,
                                                      updatedProfile.tgSelectTimeout)

        persistProfiles()
        return true
    }

    function liveTalkgroup() {
        if (!selectedProfile)
            return 0

        if (ReflectorClient.isDisconnected)
            return selectedProfile.talkgroup

        return ReflectorClient.selectedTalkgroup
    }

    function availableTalkgroups() {
        const talkgroups = []

        function addTalkgroup(value) {
            if (value === undefined || value === null || value === "")
                return

            const normalized = ProfileUtils.normalizeTalkgroup(value)
            if (talkgroups.indexOf(normalized) < 0)
                talkgroups.push(normalized)
        }

        if (selectedProfile) {
            addTalkgroup(liveTalkgroup())
            addTalkgroup(selectedProfile.talkgroup)

            const configuredMonitored = ProfileUtils.monitoredTalkgroupsNumbers(selectedProfile.monitoredTalkgroups)
            for (let i = 0; i < configuredMonitored.length; ++i)
                addTalkgroup(configuredMonitored[i])
        }

        const liveMonitored = ReflectorClient.monitoredTalkgroups || []
        for (let i = 0; i < liveMonitored.length; ++i)
            addTalkgroup(liveMonitored[i])

        return talkgroups
    }

    function applyTalkgroupSelection(talkgroup, persistDefault) {
        if (!selectedProfile)
            return

        const normalized = ProfileUtils.normalizeTalkgroup(talkgroup)

        if (ReflectorClient.isDisconnected || persistDefault)
            updateProfileTalkgroup(selectedProfile.profileId, normalized)

        if (!ReflectorClient.isDisconnected)
            ReflectorClient.selectTalkgroup(normalized)
    }

    function connectSelectedProfile() {
        if (!selectedProfile || !ProfileUtils.isProfileComplete(selectedProfile))
            return

        ReflectorClient.updateProfileConfiguration(
            selectedProfile.talkgroup,
            selectedProfile.monitoredTalkgroups,
            selectedProfile.tgSelectTimeout
        )

        ReflectorClient.connectToServer(
            selectedProfile.host,
            selectedProfile.port,
            selectedProfile.authKey,
            selectedProfile.callsign,
            selectedProfile.talkgroup,
            selectedProfile.monitoredTalkgroups,
            selectedProfile.tgSelectTimeout
        )
    }

    function shutdownApplication() {
        ReflectorClient.shutdownApplication()
    }

    function profileEndpoint(profile) {
        return profile ? profile.host + ":" + profile.port : ""
    }

    function profileMeta(profile) {
        if (!profile)
            return ""

        const monitoredSummary = ProfileUtils.monitoredTalkgroupsSummary(profile.monitoredTalkgroups)
        return monitoredSummary.length > 0
                ? qsTr("Callsign %1 | TG %2 | Idle timeout %3s | Monitors %4")
                    .arg(profile.callsign)
                    .arg(profile.talkgroup)
                    .arg(profile.tgSelectTimeout)
                    .arg(monitoredSummary)
                : qsTr("Callsign %1 | TG %2 | Idle timeout %3s")
                    .arg(profile.callsign)
                    .arg(profile.talkgroup)
                    .arg(profile.tgSelectTimeout)
    }

    function updatePreferredAudioRoute(routeId) {
        const normalizedRoute = ProfileUtils.trimmedText(routeId) === "" ? "speaker" : routeId
        saved.preferredAudioRoute = normalizedRoute
        ReflectorClient.setPreferredAudioRoute(normalizedRoute)
    }

    function clampRxAudioLevelDb(levelDb) {
        const numericLevel = Number(levelDb)
        if (Number.isNaN(numericLevel))
            return minRxAudioLevelDb

        return Math.max(minRxAudioLevelDb, Math.min(maxRxAudioLevelDb, numericLevel))
    }

    function clampTxAudioLevelDb(levelDb) {
        const numericLevel = Number(levelDb)
        if (Number.isNaN(numericLevel))
            return 0.0
        return Math.max(minTxAudioLevelDb, Math.min(maxTxAudioLevelDb, numericLevel))
    }

    function updateRxAudioLevel(levelDb) {
        const normalizedLevel = clampRxAudioLevelDb(levelDb)
        saved.rxAudioLevelDb = normalizedLevel
        ReflectorClient.setRxAudioLevelDb(normalizedLevel)
    }

    function updateTxAudioLevel(levelDb) {
        const normalizedLevel = clampTxAudioLevelDb(levelDb)
        saved.txAudioLevelDb = normalizedLevel
        ReflectorClient.setTxAudioLevelDb(normalizedLevel)
    }

    function normalizeTxTimeoutSeconds(seconds) {
        const numericValue = Number(seconds)
        if (!Number.isFinite(numericValue) || !Number.isInteger(numericValue) || numericValue <= 0)
            return 175
        return numericValue
    }

    function updateTxTimeoutSeconds(seconds) {
        const normalizedSeconds = normalizeTxTimeoutSeconds(seconds)
        saved.txTimeoutSeconds = normalizedSeconds
        ReflectorClient.setTxTimeoutSeconds(normalizedSeconds)
    }

    function normalizePttHangTimeMs(milliseconds) {
        const numericValue = Number(milliseconds)
        if (!Number.isFinite(numericValue) || !Number.isInteger(numericValue) || numericValue < 0)
            return 100
        return Math.min(numericValue, 1000)
    }

    function updatePttHangTimeMs(milliseconds) {
        const normalizedMilliseconds = normalizePttHangTimeMs(milliseconds)
        saved.pttHangTimeMs = normalizedMilliseconds
        ReflectorClient.setPttHangTimeMs(normalizedMilliseconds)
    }

    function updateTapToTalkButtonVisible(visible) {
        saved.tapToTalkButtonVisible = !!visible
    }

    function updateLiveTranscriptionEnabled(enabled) {
        const allowLiveTranscription = uiMetrics.liveTranscriptionAllowed
        const normalizedEnabled = allowLiveTranscription && !!enabled
        ReflectorClient.setLiveTranscriptionEnabled(normalizedEnabled)
        if (!normalizedEnabled)
            saved.liveTranscriptionEnabled = false
    }

    Component.onCompleted: {
        loadProfiles()
        loadNodeInfoProperties()

        const savedAudioRoute = ProfileUtils.trimmedText(saved.preferredAudioRoute)
        if (savedAudioRoute !== "") {
            Qt.callLater(function() {
                ReflectorClient.setPreferredAudioRoute(savedAudioRoute)
            })
        }

        Qt.callLater(function() {
            window.updateRxAudioLevel(saved.rxAudioLevelDb)
            window.updateTxAudioLevel(saved.txAudioLevelDb)
            window.updateTxTimeoutSeconds(saved.txTimeoutSeconds)
            window.updatePttHangTimeMs(saved.pttHangTimeMs)
            window.updateLiveTranscriptionEnabled(saved.liveTranscriptionEnabled)
        })
    }

    onSelectedProfileIdChanged: {
        refreshSelectedProfile()
        saved.selectedProfileId = selectedProfileId
    }

    Dialog {
        id: deleteProfileDialog

        modal: true
        padding: 20
        title: qsTr("Delete Profile")
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        width: Math.min(window.width - 32, 320)
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            window.deleteProfile(window.pendingDeleteIndex)
            window.pendingDeleteIndex = -1
            window.pendingDeleteName = ""
        }

        onRejected: {
            window.pendingDeleteIndex = -1
            window.pendingDeleteName = ""
        }

        contentItem: Label {
            width: deleteProfileDialog.width - 40
            text: qsTr("Delete \"%1\"?").arg(window.pendingDeleteName)
            wrapMode: Text.WordWrap
        }
    }

    StackView {
        id: stackView

        anchors.fill: parent
        initialItem: homePageComponent
    }

    QuickProfileDialog {
        id: quickProfileDialog

        profilesModel: profilesModel
        selectedProfileId: window.selectedProfileId
        connected: !ReflectorClient.isDisconnected
        busy: window.pendingProfileSwitchId !== ""
        busyProfileId: window.pendingProfileSwitchId

        onProfileChosen: profileId => window.requestProfileActivation(profileId)
    }

    QuickTalkgroupDialog {
        id: quickTalkgroupDialog

        profileName: window.selectedProfile ? window.selectedProfile.name : ""
        connected: !ReflectorClient.isDisconnected
        defaultTalkgroup: window.selectedProfile ? window.selectedProfile.talkgroup : 9
        currentTalkgroup: window.liveTalkgroup()
        availableTalkgroups: window.availableTalkgroups()

        onTalkgroupChosen: (talkgroup, persistDefault) => {
            window.applyTalkgroupSelection(talkgroup, persistDefault)
        }
    }

    Component {
        id: homePageComponent

        HomePage {
            uiMetrics: uiMetrics
            contentPadding: window.contentPadding
            safeAreaTop: window.SafeArea.margins.top
            safeAreaLeft: window.SafeArea.margins.left
            safeAreaRight: window.SafeArea.margins.right
            safeAreaBottom: window.SafeArea.margins.bottom
            surfaceColor: window.surfaceColor
            borderColor: window.borderColor
            reflectorClient: ReflectorClient
            selectedProfile: window.selectedProfile
            profilesCount: profilesModel.count
            selectedProfileEndpoint: window.profileEndpoint(window.selectedProfile)
            selectedProfileMeta: window.profileMeta(window.selectedProfile)
            canConnect: window.selectedProfile && ProfileUtils.isProfileComplete(window.selectedProfile)
            currentTalkgroup: window.liveTalkgroup()
            canSwitchProfile: !!window.selectedProfile
                              && profilesModel.count > 1
                              && window.pendingProfileSwitchId === ""
                              && !ReflectorClient.pttActive
                              && (ReflectorClient.isDisconnected
                                  || ReflectorClient.connectionStatus.startsWith("Connected"))
            canSwitchTalkgroup: !!window.selectedProfile
                                && window.pendingProfileSwitchId === ""
                                && !ReflectorClient.pttActive
                                && (ReflectorClient.isDisconnected
                                    || ReflectorClient.connectionStatus.startsWith("Connected"))
            profileSwitchInProgress: window.pendingProfileSwitchId !== ""
            pendingProfileName: window.pendingProfileSwitchName
            rxMeterLevel: ReflectorClient.rxMeterLevel
            rxMeterPeakLevel: ReflectorClient.rxMeterPeakLevel
            txMeterLevel: ReflectorClient.txMeterLevel
            txMeterPeakLevel: ReflectorClient.txMeterPeakLevel
            tapToTalkButtonVisible: saved.tapToTalkButtonVisible

            onOpenSettingsRequested: stackView.push(settingsPageComponent)
            onOpenProfileSwitcherRequested: quickProfileDialog.open()
            onOpenTalkgroupSwitcherRequested: quickTalkgroupDialog.open()
            onConnectRequested: window.connectSelectedProfile()
            onDisconnectRequested: ReflectorClient.disconnectFromServer()
            onShutdownRequested: window.shutdownApplication()
            onPttRequested: ReflectorClient.togglePtt()
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPage {
            uiMetrics: uiMetrics
            contentPadding: window.contentPadding
            safeAreaTop: window.SafeArea.margins.top
            safeAreaLeft: window.SafeArea.margins.left
            safeAreaRight: window.SafeArea.margins.right
            safeAreaBottom: window.SafeArea.margins.bottom
            surfaceColor: window.surfaceColor
            tintedSurfaceColor: window.tintedSurfaceColor
            borderColor: window.borderColor
            accentColor: window.accentColor
            reflectorClient: ReflectorClient
            profilesModel: profilesModel
            nodeInfoReadOnlyEntries: ReflectorClient.nodeInfoReadOnlyEntries
            nodeInfoReservedKeys: window.nodeInfoReservedKeys
            nodeInfoPropertiesModel: nodeInfoPropertiesModel
            selectedProfileId: window.selectedProfileId
            profileSwitchInProgress: window.pendingProfileSwitchId !== ""
            availableAudioRoutes: ReflectorClient.availableAudioRoutes
            currentAudioRoute: ReflectorClient.currentAudioRoute
            preferredAudioRoute: ReflectorClient.preferredAudioRoute
            rxAudioLevelDb: ReflectorClient.rxAudioLevelDb
            txAudioLevelDb: ReflectorClient.txAudioLevelDb
            txTimeoutSeconds: ReflectorClient.txTimeoutSeconds
            pttHangTimeMs: ReflectorClient.pttHangTimeMs
            tapToTalkButtonVisible: saved.tapToTalkButtonVisible

            onBackRequested: stackView.pop()
            onAddProfileRequested: {
                stackView.push(profileEditorPageComponent, {
                    editIndex: -1,
                    draftProfile: window.createProfileSeed()
                })
            }
            onSelectProfileRequested: profileId => window.requestProfileActivation(profileId)
            onEditProfileRequested: (editIndex, draftProfile) => {
                stackView.push(profileEditorPageComponent, {
                    editIndex: editIndex,
                    draftProfile: draftProfile
                })
            }
            onDeleteProfileRequested: editIndex => window.requestDeleteProfile(editIndex)
            onAudioRouteRequested: routeId => window.updatePreferredAudioRoute(routeId)
            onRxAudioLevelRequested: levelDb => window.updateRxAudioLevel(levelDb)
            onTxAudioLevelRequested: levelDb => window.updateTxAudioLevel(levelDb)
            onTxTimeoutSecondsRequested: seconds => window.updateTxTimeoutSeconds(seconds)
            onPttHangTimeMsRequested: milliseconds => window.updatePttHangTimeMs(milliseconds)
            onTapToTalkButtonVisibleRequested: visible => window.updateTapToTalkButtonVisible(visible)
            onHardwarePttEnabledRequested: enabled => ReflectorClient.setHardwarePttEnabled(enabled)
            onLearnedHardwarePttKeyCodeRequested: keyCode => ReflectorClient.setLearnedHardwarePttKeyCode(keyCode)
            onClearLearnedHardwarePttKeyCodeRequested: ReflectorClient.clearLearnedHardwarePttKeyCode()
            onLiveTranscriptionEnabledRequested: enabled => window.updateLiveTranscriptionEnabled(enabled)
            onTranscriptionLanguageDownloadRequested: languageTag => ReflectorClient.downloadTranscriptionModel(languageTag)
            onTranscriptionLanguageSettingsRequested: ReflectorClient.openTranscriptionSettings()
            onAddNodeInfoPropertyRequested: window.addNodeInfoProperty()
            onUpdateNodeInfoPropertyRequested: (index, key, value) => window.updateNodeInfoProperty(index, key, value)
            onDeleteNodeInfoPropertyRequested: index => window.deleteNodeInfoProperty(index)
            onBatteryOptimizationRequested: BatteryOptimizationHandler.requestBatteryOptimizationInstructions()
        }
    }

    Component {
        id: profileEditorPageComponent

        ProfileEditorPage {
            contentPadding: window.contentPadding
            safeAreaTop: window.SafeArea.margins.top
            safeAreaLeft: window.SafeArea.margins.left
            safeAreaRight: window.SafeArea.margins.right
            safeAreaBottom: window.SafeArea.margins.bottom
            surfaceColor: window.surfaceColor
            borderColor: window.borderColor

            onBackRequested: stackView.pop()
            onSaveRequested: (editIndex, profileData) => {
                if (window.upsertProfile(profileData, editIndex))
                    stackView.pop()
            }
        }
    }
}
