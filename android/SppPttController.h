/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SPPPTTCONTROLLER_H
#define SPPPTTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QObject>
#include <QString>
#include <QVariantList>

class ReflectorClient;
class SppPttBridge;

class SppPttController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString learnedSppDeviceName READ learnedSppDeviceName
               NOTIFY learnedSppDeviceChanged)
    Q_PROPERTY(QString learnedSppDeviceAddress READ learnedSppDeviceAddress
               NOTIFY learnedSppDeviceChanged)
    Q_PROPERTY(QVariantList pairedSppDevices READ pairedSppDevices
               NOTIFY pairedSppDevicesChanged)

public:
    explicit SppPttController(ReflectorClient *reflectorClient,
                              QObject *parent = nullptr);
    ~SppPttController() override;

    QString learnedSppDeviceName() const { return m_deviceName; }
    QString learnedSppDeviceAddress() const { return m_deviceAddress; }
    QVariantList pairedSppDevices() const { return m_pairedDevices; }

    Q_INVOKABLE void clearLearnedSppDevice();
    Q_INVOKABLE void refreshPairedDevices();
    Q_INVOKABLE void selectSppDevice(const QString &name, const QString &address);

signals:
    void learnedSppDeviceChanged();
    void pairedSppDevicesChanged();

private slots:
    void onHardwarePttSettingsChanged();
    void onHardwarePttLearningActiveChanged();

private:
    void loadLearnedDevice();
    void startBridgeIfNeeded();
    void stopBridge();

    ReflectorClient *m_reflectorClient = nullptr;
    SppPttBridge    *m_bridge = nullptr;
    QString          m_deviceName;
    QString          m_deviceAddress;
    QString          m_pressPattern;
    QString          m_releasePattern;
    QVariantList     m_pairedDevices;
};

#endif // SPPPTTCONTROLLER_H