#include "AndroidAudioRouteInterop.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {
const QString kAudioRouteSpeaker = QStringLiteral("speaker");
const QString kAudioRouteWiredHeadset = QStringLiteral("wired_headset");
const QString kAudioRouteBluetooth = QStringLiteral("bluetooth");
}

namespace AndroidAudioRouteInterop {

QString normalizeRouteId(const QString &routeId)
{
    const QString normalized = routeId.trimmed().toLower();
    if (normalized == kAudioRouteWiredHeadset) {
        return normalized;
    }
    if (normalized == kAudioRouteBluetooth
            || normalized.startsWith(QStringLiteral("bluetooth:"))) {
        return normalized;
    }
    return kAudioRouteSpeaker;
}

QStringList parseRoutesJson(const QString &routesJson)
{
    QStringList routeIds;

    const QJsonDocument document = QJsonDocument::fromJson(routesJson.toUtf8());
    if (document.isArray()) {
        const QJsonArray routesArray = document.array();
        routeIds.reserve(routesArray.size());
        for (const QJsonValue &routeValue : routesArray) {
            if (!routeValue.isString()) {
                continue;
            }

            const QString normalizedRoute = normalizeRouteId(routeValue.toString());
            if (!routeIds.contains(normalizedRoute)) {
                routeIds.append(normalizedRoute);
            }
        }
    }

    if (!routeIds.contains(kAudioRouteSpeaker)) {
        routeIds.append(kAudioRouteSpeaker);
    }

    return routeIds;
}

} // namespace AndroidAudioRouteInterop
