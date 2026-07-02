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

/*
 * ReflectorClientSpp.cpp
 *
 * Manages the Bluetooth SPP PTT bridge lifecycle on behalf of ReflectorClient.
 * Devices that use the Zello/PoC accessory protocol (e.g. Inrico B02) send
 * "+PTT=P" / "+PTT=R" over a Classic Bluetooth RFCOMM channel rather than
 * generating Android KeyEvents.  This file is intentionally kept separate
 * from ReflectorClientPtt.cpp (which handles KeyEvent-based hardware PTT)
 * to keep each input path self-contained and easy to reason about.
 *
 * Entry points called from the rest of the codebase:
 *   ReflectorClient::startSppPttBridgeIfNeeded()   – called after SPP device learned
 *   ReflectorClient::clearLearnedSppDevice()        – Q_INVOKABLE from QML
 */

#include "ReflectorClient.h"
#include "SppPttBridge.h"

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QJniObject>
#  include <QNativeInterface>
#endif

void ReflectorClient::clearLearnedSppDevice()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "yo6say/latry/HardwarePttSettingsStore",
        "clearLearnedSppDevice",
        "(Landroid/content/Context;)V",
        activity.object<jobject>());
#endif

    m_learnedSppDeviceName.clear();
    m_learnedSppDeviceAddress.clear();
    emit hardwarePttSettingsChanged();

    if (m_sppPttBridge) {
        m_sppPttBridge->setEnabled(false);
        delete m_sppPttBridge;
        m_sppPttBridge = nullptr;
    }
}

void ReflectorClient::startSppPttBridgeIfNeeded()
{
    if (m_learnedSppDeviceAddress.isEmpty()) {
        return;
    }

    if (!m_sppPttBridge) {
        m_sppPttBridge = new SppPttBridge(this);
        connect(m_sppPttBridge, &SppPttBridge::pttButtonPressed,
                this, &ReflectorClient::pttPressed);
        connect(m_sppPttBridge, &SppPttBridge::pttButtonReleased,
                this, &ReflectorClient::pttReleased);
    }

    m_sppPttBridge->selectDevice(m_learnedSppDeviceName, m_learnedSppDeviceAddress);
    m_sppPttBridge->setEnabled(true);
}