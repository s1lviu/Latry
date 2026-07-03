/*
 * SppPttBridge - connects to a Bluetooth SPP (Serial Port Profile) PTT
 * button and forwards press/release events to ReflectorClient via signals.
 *
 * Supports any SPP PTT device that sends distinct ASCII strings for press
 * and release events, regardless of the specific protocol used. The press
 * and release patterns are learned via startLearning() / learningComplete,
 * or supplied directly via selectDevice().
 *
 * Tested with: Inrico B02 (Zello accessory protocol, "+PTT=P" / "+PTT=R")
 *
 * CMakeLists.txt:
 *   find_package(Qt6 REQUIRED COMPONENTS ... Bluetooth)
 *   target_link_libraries(applatry PRIVATE ... Qt6::Bluetooth)
 *
 * AndroidManifest.xml:
 *   <uses-permission android:name="android.permission.BLUETOOTH_CONNECT"/>
 *   <uses-permission android:name="android.permission.BLUETOOTH_SCAN"
 *       android:usesPermissionFlags="neverForLocation"/>
 */

#ifndef SPPPTTBRIDGE_H
#define SPPPTTBRIDGE_H

#include <QObject>
#include <QBluetoothSocket>

class SppPttBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString pairedDeviceName READ pairedDeviceName NOTIFY pairedDeviceChanged)
    Q_PROPERTY(QString pairedDeviceAddress READ pairedDeviceAddress NOTIFY pairedDeviceChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    explicit SppPttBridge(QObject *parent = nullptr);
    ~SppPttBridge() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    QString status() const { return m_status; }
    QString pairedDeviceName() const { return m_deviceName; }
    QString pairedDeviceAddress() const { return m_deviceAddress; }
    bool isConnected() const;

    // Connect to the given device using the supplied press/release patterns.
    // If patterns are empty, the bridge will match any incoming data and
    // rely on learning mode to populate them.
    Q_INVOKABLE void selectDevice(const QString &name, const QString &address,
                                  const QString &pressPattern = QString(),
                                  const QString &releasePattern = QString());

    // Simulate a PTT press/release for testing without a physical device.
    Q_INVOKABLE void simulatePtt(bool pressed);

    // Learning mode: the next press+release sequence received over SPP is
    // captured and emitted via learningComplete() instead of triggering PTT.
    Q_INVOKABLE void startLearning();
    Q_INVOKABLE void stopLearning();

signals:
    void enabledChanged();
    void statusChanged();
    void pairedDeviceChanged();
    void connectedChanged();

    // Wired to ReflectorClient::pttPressed() / pttReleased() by SppPttController.
    void pttButtonPressed();
    void pttButtonReleased();

    // Emitted when learning mode captures a complete press+release sequence.
    void learningComplete(const QString &pressPattern, const QString &releasePattern);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QBluetoothSocket::SocketError error);

private:
    void setStatus(const QString &status);
    void connectToSavedDevice();
    void doConnectSocket();
    void processLine(const QByteArray &line);

    bool    m_enabled  = false;
    bool    m_learning = false;
    QString m_status   = QStringLiteral("Disabled");
    QString m_deviceName;
    QString m_deviceAddress;
    QString m_pressPattern;
    QString m_releasePattern;
    QString m_learnedPress;

    QBluetoothSocket *m_socket = nullptr;
    QByteArray        m_rxBuffer;
};

#endif // SPPPTTBRIDGE_H