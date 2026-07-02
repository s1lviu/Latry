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

#ifndef SPPPTTCONTROLLER_H
#define SPPPTTCONTROLLER_H

#include <QObject>
#include <QString>

class ReflectorClient;
class SppPttBridge;

/*
 * SppPttController
 *
 * QML-facing controller for Bluetooth SPP PTT devices (e.g. Inrico B02).
 * Follows the same pattern as BatteryOptimizationHandler: a standalone
 * QObject registered as a QML singleton in main.cpp, so that ReflectorClient
 * and latryservice remain completely unaware of SPP/Bluetooth concerns.
 *
 * Responsibilities:
 *  - Exposes learned SPP device name/address to QML.
 *  - Exposes clearLearnedSppDevice() as a Q_INVOKABLE for the settings UI.
 *  - Owns and manages the SppPttBridge lifecycle.
 *  - Connects SppPttBridge press/release signals to ReflectorClient PTT.
 *  - Listens for hardwarePttSettingsChanged() from ReflectorClient to know
 *    when a new SPP device has been learned via SppPttScanner.
 */
class SppPttController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString learnedSppDeviceName READ learnedSppDeviceName
               NOTIFY learnedSppDeviceChanged)
    Q_PROPERTY(QString learnedSppDeviceAddress READ learnedSppDeviceAddress
               NOTIFY learnedSppDeviceChanged)

public:
    explicit SppPttController(ReflectorClient *reflectorClient,
                              QObject *parent = nullptr);
    ~SppPttController() override;

    QString learnedSppDeviceName() const { return m_deviceName; }
    QString learnedSppDeviceAddress() const { return m_deviceAddress; }

    Q_INVOKABLE void clearLearnedSppDevice();

signals:
    void learnedSppDeviceChanged();

private slots:
    void onHardwarePttSettingsChanged();

private:
    void loadLearnedDevice();
    void startBridgeIfNeeded();
    void stopBridge();

    ReflectorClient *m_reflectorClient = nullptr;
    SppPttBridge    *m_bridge = nullptr;
    QString          m_deviceName;
    QString          m_deviceAddress;
};

#endif // SPPPTTCONTROLLER_H