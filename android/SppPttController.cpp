/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "SppPttController.h"
#include "SppPttBridge.h"
#include "ReflectorClient.h"
#include <QDebug>

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QJniObject>
#endif

// Helper: safely call a static Java method returning String, returning
// an empty QString on any JNI error rather than crashing.
#if defined(Q_OS_ANDROID)
static QString safeJniGetString(const char *clazz,
                                const char *method,
                                jobject context)
{
    if (!context) {
        return {};
    }
    try {
        QJniObject result = QJniObject::callStaticObjectMethod(
            clazz, method,
            "(Landroid/content/Context;)Ljava/lang/String;",
            context);
        return result.isValid() ? result.toString() : QString{};
    } catch (...) {
        qWarning() << "SppPttController: JNI call failed:" << method;
        return {};
    }
}

static QJniObject safeGetContext()
{
    try {
        return QJniObject::callStaticObjectMethod(
            "org/qtproject/qt/android/QtNative",
            "activity",
            "()Landroid/app/Activity;");
    } catch (...) {
        qWarning() << "SppPttController: failed to get Android context";
        return {};
    }
}
#endif

SppPttController::SppPttController(ReflectorClient *reflectorClient, QObject *parent)
    : QObject(parent)
    , m_reflectorClient(reflectorClient)
{
    if (m_reflectorClient) {
        connect(m_reflectorClient, &ReflectorClient::hardwarePttSettingsChanged,
                this, &SppPttController::onHardwarePttSettingsChanged);
    }

    // Defer JNI calls slightly to ensure the Android runtime is fully ready.
    QMetaObject::invokeMethod(this, [this]() {
        loadLearnedDevice();
        startBridgeIfNeeded();
    }, Qt::QueuedConnection);
}

SppPttController::~SppPttController()
{
    stopBridge();
}

void SppPttController::clearLearnedSppDevice()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = safeGetContext();
    if (activity.isValid()) {
        try {
            QJniObject::callStaticMethod<void>(
                "yo6say/latry/HardwarePttSettingsStore",
                "clearLearnedSppDevice",
                "(Landroid/content/Context;)V",
                activity.object<jobject>());
        } catch (...) {
            qWarning() << "SppPttController: failed to clear SPP device in Java";
        }
    }
#endif

    m_deviceName.clear();
    m_deviceAddress.clear();
    emit learnedSppDeviceChanged();

    stopBridge();
}

void SppPttController::onHardwarePttSettingsChanged()
{
    loadLearnedDevice();
    startBridgeIfNeeded();
}

void SppPttController::loadLearnedDevice()
{
    QString name;
    QString address;

#if defined(Q_OS_ANDROID)
    QJniObject activity = safeGetContext();
    if (activity.isValid()) {
        jobject ctx = activity.object<jobject>();
        name    = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                   "getLearnedSppName", ctx);
        address = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                   "getLearnedSppAddress", ctx);
    }
#endif

    if (name == m_deviceName && address == m_deviceAddress) {
        return;
    }

    m_deviceName    = name;
    m_deviceAddress = address;
    qDebug() << "SppPttController: loaded device" << m_deviceName << m_deviceAddress;
    emit learnedSppDeviceChanged();
}

void SppPttController::startBridgeIfNeeded()
{
    if (m_deviceAddress.isEmpty()) {
        return;
    }

    if (!m_bridge) {
        m_bridge = new SppPttBridge(this);
        if (m_reflectorClient) {
            connect(m_bridge, &SppPttBridge::pttButtonPressed,
                    m_reflectorClient, &ReflectorClient::pttPressed);
            connect(m_bridge, &SppPttBridge::pttButtonReleased,
                    m_reflectorClient, &ReflectorClient::pttReleased);
        }
    }

    m_bridge->selectDevice(m_deviceName, m_deviceAddress);
    m_bridge->setEnabled(true);
}

void SppPttController::stopBridge()
{
    if (m_bridge) {
        m_bridge->setEnabled(false);
        delete m_bridge;
        m_bridge = nullptr;
    }
}