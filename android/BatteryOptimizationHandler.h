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

#ifndef BATTERYOPTIMIZATIONHANDLER_H
#define BATTERYOPTIMIZATIONHANDLER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class BatteryOptimizationHandler : public QObject
{
    Q_OBJECT
public:
    explicit BatteryOptimizationHandler(QObject *parent = nullptr);

    Q_INVOKABLE void requestBatteryOptimizationInstructions();

signals:
    void showBatteryOptimizationInstructions(const QString &instructions);

private slots:
    void onApiResult(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkAccessManager;
    QString getGenericInstructions() const;
};

#endif // BATTERYOPTIMIZATIONHANDLER_H
