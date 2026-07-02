/*
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "SppPttBridge.h"
#include <QBluetoothUuid>
#include <QDebug>
#include <QTimer>

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QFuture>
#  include <QFutureWatcher>
#endif

// Standard Serial Port Profile UUID - the Inrico B02 exposes an SPP RFCOMM
// channel using this UUID once paired.
static const QBluetoothUuid kSppUuid(QStringLiteral("00001101-0000-1000-8000-00805F9B34FB"));

SppPttBridge::SppPttBridge(QObject *parent)
    : QObject(parent)
{
}

SppPttBridge::~SppPttBridge()
{
    if (m_discoveryAgent) {
        m_discoveryAgent->stop();
    }
    if (m_socket) {
        m_socket->close();
    }
}

void SppPttBridge::setEnabled(bool on)
{
    if (m_enabled == on)
        return;

    m_enabled = on;
    emit enabledChanged();

    if (m_enabled) {
        connectToSavedDevice();
    } else {
        disconnectDevice();
        setStatus(QStringLiteral("Disabled"));
    }
}

bool SppPttBridge::isConnected() const
{
    return m_socket && m_socket->state() == QBluetoothSocket::SocketState::ConnectedState;
}

void SppPttBridge::startScan()
{
    if (m_scanning)
        return;

    m_discoveredDevices.clear();
    emit discoveredDevicesChanged();

    if (!m_discoveryAgent) {
        m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
                this, &SppPttBridge::onDeviceDiscovered);
        connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
                this, &SppPttBridge::onDiscoveryFinished);
        connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled,
                this, &SppPttBridge::onDiscoveryFinished);
    }

    m_scanning = true;
    emit scanningChanged();
    setStatus(QStringLiteral("Scanning for Bluetooth devices..."));
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
}

void SppPttBridge::stopScan()
{
    if (m_discoveryAgent) {
        m_discoveryAgent->stop();
    }
}

void SppPttBridge::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    // Only list classic (SPP-capable) devices - the B02 is not BLE.
    if (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration
        && !(info.coreConfigurations() & QBluetoothDeviceInfo::BaseRateCoreConfiguration)) {
        return;
    }

    QVariantMap entry;
    entry["name"] = info.name();
    entry["address"] = info.address().toString();
    m_discoveredDevices.append(entry);
    emit discoveredDevicesChanged();
}

void SppPttBridge::onDiscoveryFinished()
{
    m_scanning = false;
    emit scanningChanged();
    setStatus(m_discoveredDevices.isEmpty()
                  ? QStringLiteral("No devices found")
                  : QStringLiteral("Scan finished - select a device"));
}

void SppPttBridge::selectDevice(const QString &name, const QString &address)
{
    m_deviceName = name;
    m_deviceAddress = address;
    emit pairedDeviceChanged();

    if (m_enabled) {
        connectToSavedDevice();
    }
}

void SppPttBridge::connectToSavedDevice()
{
    if (m_deviceAddress.isEmpty()) {
        setStatus(QStringLiteral("No device selected"));
        return;
    }

#if defined(Q_OS_ANDROID)
    // Android 12+ (API 31+) requires runtime grant of BLUETOOTH_CONNECT and
    // BLUETOOTH_SCAN (the latter is needed because connectToService() with a
    // UUID performs an SDP service discovery), even though both are
    // declared in the manifest. requestPermissions() (plural) is not
    // linkable in this Qt build, so we request them sequentially using the
    // singular requestPermission(), same pattern as RECORD_AUDIO elsewhere.
    const QString connectPerm = QStringLiteral("android.permission.BLUETOOTH_CONNECT");
    const QString scanPerm = QStringLiteral("android.permission.BLUETOOTH_SCAN");

    if (QtAndroidPrivate::checkPermission(connectPerm).result() == QtAndroidPrivate::PermissionResult::Denied) {
        setStatus(QStringLiteral("Requesting Bluetooth permission..."));
        auto future = QtAndroidPrivate::requestPermission(connectPerm);
        auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            auto result = watcher->result();
            QMetaObject::invokeMethod(watcher, "deleteLater", Qt::QueuedConnection);
            if (result == QtAndroidPrivate::PermissionResult::Authorized) {
                connectToSavedDevice(); // re-enter to check/request BLUETOOTH_SCAN next
            } else {
                qWarning() << "BLUETOOTH_CONNECT denied by user";
                setStatus(QStringLiteral("Bluetooth permission denied"));
            }
        });
        watcher->setFuture(future);
        return;
    }

    if (QtAndroidPrivate::checkPermission(scanPerm).result() == QtAndroidPrivate::PermissionResult::Denied) {
        setStatus(QStringLiteral("Requesting Bluetooth scan permission..."));
        auto future = QtAndroidPrivate::requestPermission(scanPerm);
        auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            auto result = watcher->result();
            QMetaObject::invokeMethod(watcher, "deleteLater", Qt::QueuedConnection);
            if (result == QtAndroidPrivate::PermissionResult::Authorized) {
                doConnectSocket();
            } else {
                qWarning() << "BLUETOOTH_SCAN denied by user";
                setStatus(QStringLiteral("Bluetooth permission denied"));
            }
        });
        watcher->setFuture(future);
        return;
    }
#endif

    doConnectSocket();
}

void SppPttBridge::doConnectSocket()
{
    disconnectDevice();

    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);

    connect(m_socket, &QBluetoothSocket::connected, this, &SppPttBridge::onSocketConnected);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &SppPttBridge::onSocketDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead, this, &SppPttBridge::onSocketReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &SppPttBridge::onSocketError);

    setStatus(QStringLiteral("Connecting to %1...").arg(m_deviceName));
    m_socket->connectToService(QBluetoothAddress(m_deviceAddress), kSppUuid,
                                QIODevice::ReadWrite);
}

void SppPttBridge::reconnect()
{
    if (m_enabled) {
        connectToSavedDevice();
    }
}

void SppPttBridge::disconnectDevice()
{
    if (m_socket) {
        m_socket->disconnect();
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
        emit connectedChanged();
    }
    m_rxBuffer.clear();
}

void SppPttBridge::onSocketConnected()
{
    setStatus(QStringLiteral("Connected to %1").arg(m_deviceName));
    emit connectedChanged();
}

void SppPttBridge::onSocketDisconnected()
{
    setStatus(QStringLiteral("Disconnected"));
    emit connectedChanged();

    // Auto-retry after a short delay if still enabled (e.g. B02 went out of range)
    if (m_enabled) {
        QTimer::singleShot(5000, this, [this]() {
            if (m_enabled && !isConnected()) {
                connectToSavedDevice();
            }
        });
    }
}

void SppPttBridge::onSocketError(QBluetoothSocket::SocketError error)
{
    Q_UNUSED(error)
    setStatus(QStringLiteral("Bluetooth error: %1").arg(m_socket ? m_socket->errorString() : QString()));
}

void SppPttBridge::onSocketReadyRead()
{
    if (!m_socket)
        return;

    m_rxBuffer.append(m_socket->readAll());

    // The B02 sends newline-terminated ASCII commands like "+ptt=p\r\n"
    int idx;
    while ((idx = m_rxBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_rxBuffer.left(idx);
        m_rxBuffer.remove(0, idx + 1);
        processLine(line);
    }

    // The Inrico B02 in practice sends "+PTT=P" / "+PTT=R" with NO trailing
    // newline at all, so we also scan the raw buffer directly for these
    // patterns and consume up to and including the match.
    const QByteArray lowerBuf = m_rxBuffer.toLower();
    int pIdx = lowerBuf.indexOf("+ptt=p");
    int rIdx = lowerBuf.indexOf("+ptt=r");
    if (pIdx >= 0) {
        emit pttButtonPressed();
        m_rxBuffer.remove(0, pIdx + 6);
    } else if (rIdx >= 0) {
        emit pttButtonReleased();
        m_rxBuffer.remove(0, rIdx + 6);
    }

    // Avoid unbounded growth if we never find a delimiter/pattern.
    if (m_rxBuffer.size() > 256) {
        m_rxBuffer.clear();
    }
}

void SppPttBridge::processLine(const QByteArray &rawLine)
{
    QByteArray line = rawLine.trimmed().toLower();
    if (line.isEmpty())
        return;

    qDebug() << "SppPttBridge: received" << line;

    if (line.contains("+ptt=p")) {
        emit pttButtonPressed();
    } else if (line.contains("+ptt=r")) {
        emit pttButtonReleased();
    }
    // Add other B02 commands here if needed, e.g. "+vol=+"/"+vol=-"
    // for the volume/group-switch buttons.
}

void SppPttBridge::simulatePtt(bool pressed)
{
    if (pressed) {
        emit pttButtonPressed();
    } else {
        emit pttButtonReleased();
    }
}

void SppPttBridge::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}