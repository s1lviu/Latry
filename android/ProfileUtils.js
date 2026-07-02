.pragma library

function trimmedText(value) {
    return value === undefined || value === null ? "" : String(value).trim()
}

function generateProfileId() {
    return "profile-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 8)
}

const defaultTgSelectTimeout = 30

function normalizePort(value) {
    const parsed = parseInt(value, 10)
    return Number.isFinite(parsed) && parsed >= 1 && parsed <= 65535 ? parsed : 5300
}

function normalizeTalkgroup(value) {
    const parsed = parseInt(value, 10)
    return Number.isFinite(parsed) && parsed >= 0 ? parsed : 9
}

function normalizeTgSelectTimeout(value) {
    const parsed = parseInt(value, 10)
    return Number.isFinite(parsed) && parsed >= 1 ? parsed : defaultTgSelectTimeout
}

function parseMonitoredTalkgroups(value) {
    const entries = []
    const seenTalkgroups = {}
    const tokens = Array.isArray(value)
            ? value
            : trimmedText(value).length > 0
              ? String(value).split(",")
              : []

    for (let index = 0; index < tokens.length; ++index) {
        const token = tokens[index]
        let talkgroupText = ""
        let priority = 0

        if (token && typeof token === "object") {
            talkgroupText = trimmedText(token.tg)
            const parsedPriority = parseInt(token.priority, 10)
            priority = Number.isFinite(parsedPriority) && parsedPriority > 0 ? parsedPriority : 0
        } else {
            const rawText = trimmedText(token)
            let endIndex = rawText.length
            while (endIndex > 0 && rawText.charAt(endIndex - 1) === "+") {
                ++priority
                --endIndex
            }
            talkgroupText = rawText.substring(0, endIndex).trim()
        }

        if (!/^\d+$/.test(talkgroupText))
            continue

        const talkgroup = parseInt(talkgroupText, 10)
        if (!Number.isFinite(talkgroup) || talkgroup <= 0)
            continue

        const dedupeKey = String(talkgroup)
        if (Object.prototype.hasOwnProperty.call(seenTalkgroups, dedupeKey))
            continue

        const display = talkgroupText + "+".repeat(priority)
        entries.push({
            tg: talkgroup,
            priority: priority,
            display: display
        })
        seenTalkgroups[dedupeKey] = true
    }

    return entries
}

function normalizeMonitoredTalkgroups(value) {
    return parseMonitoredTalkgroups(value).map(entry => entry.display).join(",")
}

function monitoredTalkgroupsNumbers(value) {
    return parseMonitoredTalkgroups(value).map(entry => entry.tg)
}

function monitoredTalkgroupsSummary(value) {
    return parseMonitoredTalkgroups(value).map(entry => entry.display).join(", ")
}

function copyProfile(profile) {
    if (!profile)
        return null

    return {
        profileId: trimmedText(profile.profileId),
        name: trimmedText(profile.name),
        host: trimmedText(profile.host),
        port: normalizePort(profile.port),
        callsign: trimmedText(profile.callsign),
        authKey: trimmedText(profile.authKey),
        talkgroup: normalizeTalkgroup(profile.talkgroup),
        monitoredTalkgroups: normalizeMonitoredTalkgroups(profile.monitoredTalkgroups),
        tgSelectTimeout: normalizeTgSelectTimeout(profile.tgSelectTimeout)
    }
}

function isProfileComplete(profile) {
    return !!profile
            && trimmedText(profile.host) !== ""
            && trimmedText(profile.callsign) !== ""
            && trimmedText(profile.authKey) !== ""
            && trimmedText(profile.talkgroup) !== ""
            && normalizePort(profile.port) > 0
}

function normalizeProfile(profile, fallbackName) {
    if (!profile)
        return null

    const host = trimmedText(profile.host)
    const callsign = trimmedText(profile.callsign).toUpperCase()
    const authKey = trimmedText(profile.authKey)
    const name = trimmedText(profile.name) || host || fallbackName || "Server"
    const normalized = {
        profileId: trimmedText(profile.profileId) || generateProfileId(),
        name: name,
        host: host,
        port: normalizePort(profile.port),
        callsign: callsign,
        authKey: authKey,
        talkgroup: normalizeTalkgroup(profile.talkgroup),
        monitoredTalkgroups: normalizeMonitoredTalkgroups(profile.monitoredTalkgroups),
        tgSelectTimeout: normalizeTgSelectTimeout(profile.tgSelectTimeout)
    }

    return isProfileComplete(normalized) ? normalized : null
}
