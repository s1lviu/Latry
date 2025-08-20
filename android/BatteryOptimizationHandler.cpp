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

#include "BatteryOptimizationHandler.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QSysInfo>
#include <QRegularExpression>
#include <QMetaObject>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

BatteryOptimizationHandler::BatteryOptimizationHandler(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
}

void BatteryOptimizationHandler::requestBatteryOptimizationInstructions()
{
#if defined(Q_OS_ANDROID)
    QJniObject manufacturer = QJniObject::getStaticObjectField("android/os/Build", "MANUFACTURER", "Ljava/lang/String;");
    QString manufacturerString = manufacturer.toString().toLower().replace(" ", "-");

    qDebug() << "Fetching DKMA instructions for manufacturer:" << manufacturerString;

    QString apiUrl = "https://dontkillmyapp.com/api/v2/" + manufacturerString + ".json";
    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Latry-VoIP-App/1.0");
    
    QNetworkReply* reply = m_networkAccessManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onApiResult(reply);
    });
#else
    qDebug() << "Not running on Android, skipping battery optimization instructions.";
#endif
}

void BatteryOptimizationHandler::onApiResult(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        if (!jsonDoc.isNull()) {
            QJsonObject jsonObj = jsonDoc.object();
            QString manufacturerName = jsonObj.value("name").toString();
            QString userSolution = jsonObj.value("user_solution").toString();
            
            if (!userSolution.isEmpty()) {
                // Replace app name placeholders as documented in DKMA API
                // Handle bracketed placeholders
                userSolution.replace("[Your app]", "Latry");
                userSolution.replace("[your app]", "Latry");
                userSolution.replace("[MyAppName]", "Latry");
                // Handle non-bracketed placeholders 
                userSolution.replace("Your app", "Latry");
                userSolution.replace("your app", "Latry");
                
                // Make images responsive by adding CSS styling
                static const QRegularExpression imgRegex("<img([^>]*)>");
                userSolution.replace(imgRegex, 
                    "<img style=\"max-width: 100%; height: auto; display: block; margin: 10px auto;\"\\1>");
                
                // Add manufacturer-specific header
                QString formattedInstructions = QString(
                    "<h3>Battery Optimization Setup for %1 Devices</h3>"
                    "<p><b>To ensure Latry works reliably in background, please follow these steps:</b></p>"
                    "%2"
                    "<hr>"
                    "<p><i>These instructions are specific to %1 devices and will help prevent the system from killing the Latry background service.</i></p>"
                    "<p><small>Instructions provided by <a href='https://dontkillmyapp.com'>dontkillmyapp.com</a></small></p>"
                ).arg(manufacturerName, userSolution);
                
                emit showBatteryOptimizationInstructions(formattedInstructions);
            } else {
                emit showBatteryOptimizationInstructions(getGenericInstructions());
            }
        } else {
            emit showBatteryOptimizationInstructions(getGenericInstructions());
        }
    } else {
        qDebug() << "Error fetching from DKMA API:" << reply->errorString();
        emit showBatteryOptimizationInstructions(getGenericInstructions());
    }
    QMetaObject::invokeMethod(reply, "deleteLater", Qt::QueuedConnection);
}

QString BatteryOptimizationHandler::getGenericInstructions() const
{
    QString manufacturer = "Unknown";
#if defined(Q_OS_ANDROID)
    QJniObject manufacturerObj = QJniObject::getStaticObjectField("android/os/Build", "MANUFACTURER", "Ljava/lang/String;");
    manufacturer = manufacturerObj.toString();
#endif

    return QString(
        "<h3>Battery Optimization Setup for %1 Devices</h3>"
        "<p><b>To ensure Latry works reliably in background, please follow these steps:</b></p>"
        "<ol>"
        "<li><b>Disable Battery Optimization:</b>"
        "<ul><li>Go to Settings → Battery → Battery Optimization</li>"
        "<li>Find \"Latry\" and select \"Don't optimize\"</li></ul></li>"
        "<li><b>Enable Auto-start:</b>"
        "<ul><li>Go to Settings → Apps → Latry → Permissions</li>"
        "<li>Enable \"Auto-start\" or \"Start up automatically\"</li></ul></li>"
        "<li><b>Allow Background Activity:</b>"
        "<ul><li>Go to Settings → Apps → Latry</li>"
        "<li>Enable \"Background activity\" or \"Run in background\"</li></ul></li>"
        "<li><b>Set Power Plan to Performance:</b>"
        "<ul><li>Some devices have power management settings</li>"
        "<li>Set to \"Performance\" or \"High Performance\" mode</li></ul></li>"
        "</ol>"
        "<hr>"
        "<p><i>These are general instructions for Android devices. Different manufacturers may have slightly different menu paths.</i></p>"
        "<p><b>Note:</b> These settings ensure the Latry background service stays active when the screen is off.</p>"
        "<p><small>Instructions provided by <a href='https://dontkillmyapp.com'>dontkillmyapp.com</a></small></p>"
    ).arg(manufacturer);
}
