import QtQuick
import QtQuick.Controls
import QtTest

import "../.." as App

TestCase {
    name: "ProfileEditorPage"
    when: windowShown

    Component {
        id: editorComponent

        App.ProfileEditorPage {
            width: 360
            height: 720
            contentPadding: 12
            safeAreaTop: 0
            safeAreaLeft: 0
            safeAreaRight: 0
            safeAreaBottom: 0
            surfaceColor: "white"
            borderColor: "#d7deee"
            editIndex: -1
            draftProfile: ({
                profileId: "profile-1",
                name: "",
                host: "reflector.example.net",
                port: 5300,
                callsign: "YO6SAY",
                authKey: "secret",
                talkgroup: 9,
                monitoredTalkgroups: "112++",
                tgSelectTimeout: 45
            })
        }
    }

    function test_formComplete_reflectsDraftFields() {
        const page = createTemporaryObject(editorComponent, this)
        verify(page !== null)
        verify(page.formComplete)

        page.draftHost = ""
        verify(!page.formComplete)

        page.draftHost = "reflector.example.net"
        page.draftAuthKey = ""
        verify(!page.formComplete)
    }

    function test_currentProfilePayload_returnsEditableValues() {
        const page = createTemporaryObject(editorComponent, this)
        verify(page !== null)

        page.draftName = "Evening Net"
        page.draftPort = "5301"
        page.draftTalkgroup = "91"
        page.draftMonitoredTalkgroups = "91++,3100"
        page.draftTgSelectTimeout = "120"

        const payload = page.currentProfilePayload()
        compare(payload.profileId, "profile-1")
        compare(payload.name, "Evening Net")
        compare(payload.host, "reflector.example.net")
        compare(payload.port, "5301")
        compare(payload.callsign, "YO6SAY")
        compare(payload.authKey, "secret")
        compare(payload.talkgroup, "91")
        compare(payload.monitoredTalkgroups, "91++,3100")
        compare(payload.tgSelectTimeout, "120")
    }

    function test_requestSave_emitsOnlyWhenFormComplete() {
        const page = createTemporaryObject(editorComponent, this)
        verify(page !== null)

        const spy = signalSpy.createObject(page, {
            target: page,
            signalName: "saveRequested"
        })
        verify(spy.valid)

        page.requestSave()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], -1)
        compare(spy.signalArguments[0][1].host, "reflector.example.net")

        page.draftCallsign = ""
        page.requestSave()
        compare(spy.count, 1)
    }

    Component {
        id: signalSpy
        SignalSpy { }
    }
}
