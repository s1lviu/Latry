/*
 * Copyright (C) 2025 Silviu YO6SAY
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

// Standard Serial Port Profile UUID
static const QBluetoothUuid kSppUuid(
        QStringLiteral("00001101-0000-1000-8000-00805F9B34FB"));

SppPttBridge::SppPttBridge(QObject *parent)
    : QObject(parent)
{
}

SppPttBridge::~SppPttBridge()
{
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
        if (m_socket) {
            m_socket->disconnect();
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
            emit connectedChanged();
        }
        m_rxBuffer.clear();
        setStatus(QStringLiteral("Disabled"));
    }
}

bool SppPttBridge::isConnected() const
{
    return m_socket
           && m_socket->state() == QBluetoothSocket::SocketState::ConnectedState;
}

void SppPttBridge::selectDevice(const QString &name,
                                const QString &address,
                                const QString &pressPattern,
                                const QString &releasePattern)
{
    m_deviceName    = name;
    m_deviceAddress = address;

    // Use provided patterns, or fall back to the Zello/Inrico B02 defaults
    // if none are given (backwards compatibility with devices using this protocol).
    m_pressPattern   = pressPattern.isEmpty()
                       ? QStringLiteral("+ptt=p") : pressPattern;
    m_releasePattern = releasePattern.isEmpty()
                       ? QStringLiteral("+ptt=r") : releasePattern;

    emit pairedDeviceChanged();

    if (m_enabled) {
        connectToSavedDevice();
    }
}

void SppPttBridge::simulatePtt(bool pressed)
{
    if (pressed)
        emit pttButtonPressed();
    else
        emit pttButtonReleased();
}

void SppPttBridge::startLearning()
{
    m_learning = true;
    m_learnedPress.clear();
    qDebug() << "SppPttBridge: learning mode started";
}

void SppPttBridge::stopLearning()
{
    m_learning = false;
    m_learnedPress.clear();
    qDebug() << "SppPttBridge: learning mode stopped";
}

void SppPttBridge::connectToSavedDevice()
{
    if (m_deviceAddress.isEmpty()) {
        setStatus(QStringLiteral("No device selected"));
        return;
    }

#if defined(Q_OS_ANDROID)
    // Android 12+ (API 31+) requires runtime grant of BLUETOOTH_CONNECT and
    // BLUETOOTH_SCAN even though both are declared in the manifest.
    const QString connectPerm = QStringLiteral("android.permission.BLUETOOTH_CONNECT");
    const QString scanPerm    = QStringLiteral("android.permission.BLUETOOTH_SCAN");

    if (QtAndroidPrivate::checkPermission(connectPerm).result()
            == QtAndroidPrivate::PermissionResult::Denied) {
        setStatus(QStringLiteral("Requesting Bluetooth permission..."));
        auto future  = QtAndroidPrivate::requestPermission(connectPerm);
        auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            auto result = watcher->result();
            QMetaObject::invokeMethod(watcher, "deleteLater", Qt::QueuedConnection);
            if (result == QtAndroidPrivate::PermissionResult::Authorized)
                connectToSavedDevice();
            else
                setStatus(QStringLiteral("Bluetooth permission denied"));
        });
        watcher->setFuture(future);
        return;
    }

    if (QtAndroidPrivate::checkPermission(scanPerm).result()
            == QtAndroidPrivate::PermissionResult::Denied) {
        setStatus(QStringLiteral("Requesting Bluetooth scan permission..."));
        auto future  = QtAndroidPrivate::requestPermission(scanPerm);
        auto *watcher = new QFutureWatcher<QtAndroidPrivate::PermissionResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            auto result = watcher->result();
            QMetaObject::invokeMethod(watcher, "deleteLater", Qt::QueuedConnection);
            if (result == QtAndroidPrivate::PermissionResult::Authorized)
                doConnectSocket();
            else
                setStatus(QStringLiteral("Bluetooth permission denied"));
        });
        watcher->setFuture(future);
        return;
    }
#endif

    doConnectSocket();
}

void SppPttBridge::doConnectSocket()
{
    if (m_socket) {
        m_socket->disconnect();
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_rxBuffer.clear();

    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_socket, &QBluetoothSocket::connected,
            this, &SppPttBridge::onSocketConnected);
    connect(m_socket, &QBluetoothSocket::disconnected,
            this, &SppPttBridge::onSocketDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead,
            this, &SppPttBridge::onSocketReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred,
            this, &SppPttBridge::onSocketError);

    setStatus(QStringLiteral("Connecting to %1...").arg(m_deviceName));
    m_socket->connectToService(QBluetoothAddress(m_deviceAddress),
                               kSppUuid, QIODevice::ReadWrite);
}

void SppPttBridge::scheduleReconnect()
{
    if (m_enabled) {
        QTimer::singleShot(5000, this, [this]() {
            if (m_enabled && !isConnected()) {
                qDebug() << "SppPttBridge: attempting reconnect to" << m_deviceName;
                connectToSavedDevice();
            }
        });
    }
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
    scheduleReconnect();
}

void SppPttBridge::onSocketError(QBluetoothSocket::SocketError error)
{
    Q_UNUSED(error)
    setStatus(QStringLiteral("Bluetooth error: %1")
              .arg(m_socket ? m_socket->errorString() : QString()));
    scheduleReconnect();
}

void SppPttBridge::onSocketReadyRead()
{
    if (!m_socket)
        return;

    m_rxBuffer.append(m_socket->readAll());

    // Learning mode: capture the first chunk as press pattern, the next
    // distinct chunk as release pattern, then emit learningComplete.
    // This runs before normal pattern matching so that the raw data is
    // captured before any transformation.
    if (m_learning && !m_rxBuffer.isEmpty()) {
        const QString chunk = QString::fromUtf8(m_rxBuffer).trimmed();
        m_rxBuffer.clear();

        if (!chunk.isEmpty()) {
            if (m_learnedPress.isEmpty()) {
                m_learnedPress = chunk;
                qDebug() << "SppPttBridge: learned press pattern:" << m_learnedPress;
            } else if (chunk != m_learnedPress) {
                qDebug() << "SppPttBridge: learned release pattern:" << chunk;
                m_learning = false;
                emit learningComplete(m_learnedPress, chunk);
                m_learnedPress.clear();
            }
        }
        return;
    }

    // Normal operation: try newline-delimited protocol first
    int idx;
    while ((idx = m_rxBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_rxBuffer.left(idx);
        m_rxBuffer.remove(0, idx + 1);
        processLine(line);
    }

    // Fallback: scan raw buffer for patterns without a newline delimiter
    // (e.g. Inrico B02 sends "+PTT=P" / "+PTT=R" without trailing newline)
    const QByteArray lowerBuf     = m_rxBuffer.toLower();
    const QByteArray lowerPress   = m_pressPattern.toLower().toUtf8();
    const QByteArray lowerRelease = m_releasePattern.toLower().toUtf8();

    if (!lowerPress.isEmpty()) {
        int pIdx = lowerBuf.indexOf(lowerPress);
        if (pIdx >= 0) {
            emit pttButtonPressed();
            m_rxBuffer.remove(0, pIdx + lowerPress.size());
            return;
        }
    }
    if (!lowerRelease.isEmpty()) {
        int rIdx = lowerBuf.indexOf(lowerRelease);
        if (rIdx >= 0) {
            emit pttButtonReleased();
            m_rxBuffer.remove(0, rIdx + lowerRelease.size());
        }
    }

    // Avoid unbounded buffer growth
    if (m_rxBuffer.size() > 256)
        m_rxBuffer.clear();
}

void SppPttBridge::processLine(const QByteArray &rawLine)
{
    const QByteArray line = rawLine.trimmed().toLower();
    if (line.isEmpty())
        return;

    const QByteArray lowerPress   = m_pressPattern.toLower().toUtf8();
    const QByteArray lowerRelease = m_releasePattern.toLower().toUtf8();

    if (!lowerPress.isEmpty() && line.contains(lowerPress))
        emit pttButtonPressed();
    else if (!lowerRelease.isEmpty() && line.contains(lowerRelease))
        emit pttButtonReleased();
}

void SppPttBridge::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}