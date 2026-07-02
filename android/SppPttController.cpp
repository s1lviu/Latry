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

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QJniObject>
#endif

SppPttController::SppPttController(ReflectorClient *reflectorClient, QObject *parent)
    : QObject(parent)
    , m_reflectorClient(reflectorClient)
{
    connect(m_reflectorClient, &ReflectorClient::hardwarePttSettingsChanged,
            this, &SppPttController::onHardwarePttSettingsChanged);

    loadLearnedDevice();
    startBridgeIfNeeded();
}

SppPttController::~SppPttController()
{
    stopBridge();
}

void SppPttController::clearLearnedSppDevice()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/HardwarePttSettingsStore",
        "clearLearnedSppDevice",
        "(Landroid/content/Context;)V",
        activity.object<jobject>());
#endif

    m_deviceName.clear();
    m_deviceAddress.clear();
    emit learnedSppDeviceChanged();

    stopBridge();
}

void SppPttController::onHardwarePttSettingsChanged()
{
    // Called when ReflectorClient's PTT settings change – this includes when
    // SppPttScanner reports a newly learned SPP device (result == 4 in JNI).
    // Re-load from SharedPreferences and start the bridge if a device is now set.
    loadLearnedDevice();
    startBridgeIfNeeded();
}

void SppPttController::loadLearnedDevice()
{
    QString name;
    QString address;

#if defined(Q_OS_ANDROID)
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");
    QJniObject nameObj = QJniObject::callStaticObjectMethod(
        "yo6say/latry/HardwarePttSettingsStore",
        "getLearnedSppName",
        "(Landroid/content/Context;)Ljava/lang/String;",
        activity.object<jobject>());
    QJniObject addrObj = QJniObject::callStaticObjectMethod(
        "yo6say/latry/HardwarePttSettingsStore",
        "getLearnedSppAddress",
        "(Landroid/content/Context;)Ljava/lang/String;",
        activity.object<jobject>());
    name    = nameObj.toString();
    address = addrObj.toString();
#endif

    if (name == m_deviceName && address == m_deviceAddress) {
        return;
    }

    m_deviceName    = name;
    m_deviceAddress = address;
    emit learnedSppDeviceChanged();
}

void SppPttController::startBridgeIfNeeded()
{
    if (m_deviceAddress.isEmpty()) {
        return;
    }

    if (!m_bridge) {
        m_bridge = new SppPttBridge(this);
        connect(m_bridge, &SppPttBridge::pttButtonPressed,
                m_reflectorClient, &ReflectorClient::pttPressed);
        connect(m_bridge, &SppPttBridge::pttButtonReleased,
                m_reflectorClient, &ReflectorClient::pttReleased);
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