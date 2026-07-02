import QtQuick
import QtTest

import "../.." as App

TestCase {
    name: "QuickTalkgroupDialog"
    when: windowShown

    Component {
        id: dialogComponent

        App.QuickTalkgroupDialog {
            profileName: "Primary"
            connected: true
            defaultTalkgroup: 9
            currentTalkgroup: 3100
            availableTalkgroups: [0, 9, 3100]
        }
    }

    Component {
        id: signalSpy
        SignalSpy { }
    }

    function test_titlesAndLabels_reflectCurrentAndDefaultState() {
        const dialog = createTemporaryObject(dialogComponent, this)
        verify(dialog !== null)

        compare(dialog.talkgroupTitle(0), "Monitor Mode")
        compare(dialog.talkgroupTitle(91), "TG 91")
        compare(dialog.quickLabel(9), "TG 9\nDefault")
        compare(dialog.quickLabel(3100), "TG 3100\nCurrent")

        dialog.defaultTalkgroup = 3100
        compare(dialog.quickLabel(3100), "TG 3100\nCurrent + Default")
    }

    function test_submitTalkgroup_validatesBeforeEmitting() {
        const dialog = createTemporaryObject(dialogComponent, this)
        verify(dialog !== null)

        const spy = signalSpy.createObject(this, {
            target: dialog,
            signalName: "talkgroupChosen"
        })
        verify(spy.valid)

        dialog.submitTalkgroup("", false)
        dialog.submitTalkgroup("abc", true)
        dialog.submitTalkgroup("-1", true)
        compare(spy.count, 0)

        dialog.submitTalkgroup(" 240 ", true)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], 240)
        compare(spy.signalArguments[0][1], true)
    }
}
