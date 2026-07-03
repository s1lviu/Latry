/*
 * Copyright (C) 2025 Silviu YO6SAY
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "SppPttController.h"
#include "SppPttBridge.h"
#include "ReflectorClient.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#  include <QJniObject>
#endif

#if defined(Q_OS_ANDROID)
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

static QString safeJniGetString(const char *clazz,
                                const char *method,
                                jobject context)
{
    if (!context) return {};
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
#endif

SppPttController::SppPttController(ReflectorClient *reflectorClient, QObject *parent)
    : QObject(parent)
    , m_reflectorClient(reflectorClient)
{
    if (m_reflectorClient) {
        connect(m_reflectorClient, &ReflectorClient::hardwarePttSettingsChanged,
                this, &SppPttController::onHardwarePttSettingsChanged);
        connect(m_reflectorClient, &ReflectorClient::hardwarePttLearningActiveChanged,
            this, &SppPttController::onHardwarePttLearningActiveChanged);
    }

    // Defer JNI calls to ensure Android runtime is fully ready.
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
    m_pressPattern.clear();
    m_releasePattern.clear();
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
    QString pressPattern;
    QString releasePattern;

#if defined(Q_OS_ANDROID)
    QJniObject activity = safeGetContext();
    if (activity.isValid()) {
        jobject ctx = activity.object<jobject>();
        name          = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                         "getLearnedSppName", ctx);
        address       = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                         "getLearnedSppAddress", ctx);
        pressPattern  = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                         "getLearnedSppPressPattern", ctx);
        releasePattern = safeJniGetString("yo6say/latry/HardwarePttSettingsStore",
                                          "getLearnedSppReleasePattern", ctx);
    }
#endif

    if (name == m_deviceName && address == m_deviceAddress
            && pressPattern == m_pressPattern && releasePattern == m_releasePattern) {
        return;
    }

    m_deviceName    = name;
    m_deviceAddress = address;
    m_pressPattern  = pressPattern;
    m_releasePattern = releasePattern;

    qDebug() << "SppPttController: loaded device" << m_deviceName
             << "press=[" << m_pressPattern << "]"
             << "release=[" << m_releasePattern << "]";

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
            connect(m_bridge, &SppPttBridge::learningComplete,
                    this, &SppPttController::onLearningComplete);
        }
    }

    m_bridge->selectDevice(m_deviceName, m_deviceAddress,
                           m_pressPattern, m_releasePattern);
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

void SppPttController::onHardwarePttLearningActiveChanged()
{
    if (!m_reflectorClient) return;

    if (m_reflectorClient->hardwarePttLearningActive()) {
        if (m_deviceAddress.isEmpty()) {
            stopBridge();
        } else if (m_bridge) {
            // Device already selected – start learning mode on bridge
            m_bridge->startLearning();
        }
    } else {
        loadLearnedDevice();
        startBridgeIfNeeded();
    }
}

void SppPttController::refreshPairedDevices()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = safeGetContext();
    if (!activity.isValid()) return;

    QJniObject json = QJniObject::callStaticObjectMethod(
        "yo6say/latry/SppDeviceHelper",
        "getPairedClassicDevicesJson",
        "(Landroid/content/Context;)Ljava/lang/String;",
        activity.object<jobject>());

    if (!json.isValid()) return;

    QJsonArray arr = QJsonDocument::fromJson(
        json.toString().toUtf8()).array();

    m_pairedDevices.clear();
    
    for (const QJsonValue &v : arr) {
        QVariantMap entry;
        entry["name"]    = v["name"].toString();
        entry["address"] = v["address"].toString();
        m_pairedDevices.append(entry);
    }
    emit pairedSppDevicesChanged();
#endif
}

void SppPttController::selectSppDevice(const QString &name, const QString &address)
{
    m_deviceName    = name;
    m_deviceAddress = address;
    m_pressPattern.clear();
    m_releasePattern.clear();

#if defined(Q_OS_ANDROID)
    // Persist the selected device immediately
    QJniObject activity = safeGetContext();
    if (activity.isValid()) {
        QJniObject jName = QJniObject::fromString(name);
        QJniObject jAddress = QJniObject::fromString(address);
        QJniObject jPress = QJniObject::fromString(QString("+PTT=P"));
        QJniObject jRelease = QJniObject::fromString(QString("+PTT=R"));
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/HardwarePttSettingsStore",
            "setLearnedSppDevice",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            activity.object<jobject>(),
            jName.object<jstring>(),
            jAddress.object<jstring>(),
            jPress.object<jstring>(),
            jRelease.object<jstring>());
    }
#endif

    emit learnedSppDeviceChanged();
    startBridgeIfNeeded();
}

void SppPttController::onLearningComplete(const QString &press, const QString &release)
{
    qDebug() << "SppPttController: learning complete press=[" << press << "] release=[" << release << "]";

    m_pressPattern  = press;
    m_releasePattern = release;

#if defined(Q_OS_ANDROID)
    QJniObject activity = safeGetContext();
    if (activity.isValid()) {
        QJniObject jName    = QJniObject::fromString(m_deviceName);
        QJniObject jAddress = QJniObject::fromString(m_deviceAddress);
        QJniObject jPress   = QJniObject::fromString(press);
        QJniObject jRelease = QJniObject::fromString(release);
        QJniObject::callStaticMethod<void>(
            "yo6say/latry/HardwarePttSettingsStore",
            "setLearnedSppDevice",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            activity.object<jobject>(),
            jName.object<jstring>(),
            jAddress.object<jstring>(),
            jPress.object<jstring>(),
            jRelease.object<jstring>());
    }
#endif

    // Update bridge with learned patterns
    m_bridge->selectDevice(m_deviceName, m_deviceAddress, press, release);

    // Cancel learning mode in ReflectorClient
    if (m_reflectorClient) {
        m_reflectorClient->cancelHardwarePttLearning();
    }

    emit learnedSppDeviceChanged();
}