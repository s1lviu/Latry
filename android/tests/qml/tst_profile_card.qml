import QtQuick
import QtTest

import "../.." as App

TestCase {
    name: "ProfileCard"
    when: windowShown

    Component {
        id: cardComponent

        App.ProfileCard {
            width: 360
            index: 3
            profileId: "profile-2"
            name: ""
            host: "reflector.example.net"
            port: 5300
            callsign: "YO6SAY"
            authKey: "secret"
            talkgroup: 9
            monitoredTalkgroups: "112++,3100"
            tgSelectTimeout: 30
            isSelected: false
            connected: false
            canUse: true
            canDelete: false
            tintedSurfaceColor: "#edf2ff"
            accentColor: "#2c5cff"
        }
    }

    function test_displayName_fallsBackToHost() {
        const card = createTemporaryObject(cardComponent, this)
        verify(card !== null)

        compare(card.displayName, "reflector.example.net")

        card.name = "Evening Net"
        compare(card.displayName, "Evening Net")
    }

    function test_activationArea_enabledOnlyWhenProfileCanBeUsed() {
        const card = createTemporaryObject(cardComponent, this)
        verify(card !== null)
        wait(0)

        const activationArea = findChild(card, "activationArea")
        verify(activationArea !== null)
        verify(activationArea.enabled)

        card.isSelected = true
        verify(!activationArea.enabled)

        card.isSelected = false
        card.canUse = false
        verify(!activationArea.enabled)
    }

    function test_actionButtons_reflectDeleteAvailability() {
        const card = createTemporaryObject(cardComponent, this)
        verify(card !== null)
        wait(0)

        const editButton = findChild(card, "editButton")
        verify(editButton !== null)
        verify(editButton.enabled)

        const deleteButton = findChild(card, "deleteButton")
        verify(deleteButton !== null)
        verify(!deleteButton.enabled)

        card.canDelete = true
        verify(deleteButton.enabled)
    }

    function test_monitoredTalkgroupsLabel_tracksConfiguredSummary() {
        const card = createTemporaryObject(cardComponent, this)
        verify(card !== null)
        wait(0)

        const monitoredLabel = findChild(card, "monitoredTalkgroupsLabel")
        verify(monitoredLabel !== null)
        tryCompare(monitoredLabel, "text", "Monitors 112++, 3100")

        card.monitoredTalkgroups = ""
        tryCompare(monitoredLabel, "text", "")
    }

    function test_editRequested_payload_preservesTgSelectTimeout() {
        const card = createTemporaryObject(cardComponent, this)
        verify(card !== null)
        wait(0)

        const spy = signalSpy.createObject(card, {
            target: card,
            signalName: "editRequested"
        })
        verify(spy.valid)

        const editButton = findChild(card, "editButton")
        verify(editButton !== null)
        editButton.click()

        tryCompare(spy, "count", 1)
        compare(spy.signalArguments[0][1].tgSelectTimeout, 30)
    }

    Component {
        id: signalSpy
        SignalSpy { }
    }
}
