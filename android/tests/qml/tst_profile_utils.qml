import QtQuick
import QtTest

import "../.." as App
import "../../ProfileUtils.js" as ProfileUtils

TestCase {
    name: "ProfileUtils"

    function test_parseMonitoredTalkgroups_fromString() {
        const entries = ProfileUtils.parseMonitoredTalkgroups(" 112++, 240, abc, 240, 0, 2403+ ")

        compare(entries.length, 3)
        compare(entries[0].tg, 112)
        compare(entries[0].priority, 2)
        compare(entries[0].display, "112++")
        compare(entries[1].tg, 240)
        compare(entries[1].priority, 0)
        compare(entries[1].display, "240")
        compare(entries[2].tg, 2403)
        compare(entries[2].priority, 1)
        compare(entries[2].display, "2403+")
    }

    function test_parseMonitoredTalkgroups_fromObjects() {
        const entries = ProfileUtils.parseMonitoredTalkgroups([
            { tg: "91", priority: "3" },
            { tg: "3100", priority: 1 },
            { tg: "91", priority: 0 },
            { tg: "bad", priority: 2 }
        ])

        compare(entries.length, 2)
        compare(entries[0].display, "91+++")
        compare(entries[1].display, "3100+")
    }

    function test_normalizeProfile_normalizesAndDefaults() {
        const profile = ProfileUtils.normalizeProfile({
            profileId: "",
            name: "  ",
            host: " reflector.example.net ",
            port: "99999",
            callsign: " yo6say ",
            authKey: " secret ",
            talkgroup: "abc",
            monitoredTalkgroups: "112++, 240, 240",
            tgSelectTimeout: "0"
        }, "Fallback")

        verify(profile !== null)
        verify(profile.profileId.indexOf("profile-") === 0)
        compare(profile.name, "reflector.example.net")
        compare(profile.host, "reflector.example.net")
        compare(profile.port, 5300)
        compare(profile.callsign, "YO6SAY")
        compare(profile.authKey, "secret")
        compare(profile.talkgroup, 9)
        compare(profile.monitoredTalkgroups, "112++,240")
        compare(profile.tgSelectTimeout, 30)
    }

    function test_copyProfile_andCompleteness() {
        const copy = ProfileUtils.copyProfile({
            profileId: "profile-1",
            name: " Primary ",
            host: " host.example ",
            port: "5301",
            callsign: "yo6say",
            authKey: " key ",
            talkgroup: "0",
            monitoredTalkgroups: " 91++,3100 ",
            tgSelectTimeout: "45"
        })

        compare(copy.profileId, "profile-1")
        compare(copy.name, "Primary")
        compare(copy.host, "host.example")
        compare(copy.port, 5301)
        compare(copy.callsign, "yo6say")
        compare(copy.authKey, "key")
        compare(copy.talkgroup, 0)
        compare(copy.monitoredTalkgroups, "91++,3100")
        compare(copy.tgSelectTimeout, 45)
        verify(ProfileUtils.isProfileComplete(copy))
        verify(!ProfileUtils.isProfileComplete({
            host: "",
            port: 5300,
            callsign: "YO6SAY",
            authKey: "secret",
            talkgroup: 9
        }))
    }
}
