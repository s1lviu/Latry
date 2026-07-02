#ifndef ANDROIDAUDIOROUTEINTEROP_H
#define ANDROIDAUDIOROUTEINTEROP_H

#include <QString>
#include <QStringList>

namespace AndroidAudioRouteInterop {

QString normalizeRouteId(const QString &routeId);
QStringList parseRoutesJson(const QString &routesJson);

} // namespace AndroidAudioRouteInterop

#endif // ANDROIDAUDIOROUTEINTEROP_H
