/*
 * SppPttBridge - connects to an Inrico B02 / similar Bluetooth SPP
 * "Zello accessory protocol" PTT button and forwards press/release
 * events to ReflectorClient::pttPressed() / pttReleased().
 *
 * Protocol observed from Inrico B02:
 *   - Pairs as a standard Bluetooth SPP (Serial Port Profile) device
 *   - Once connected, sends ASCII lines:
 *       "+ptt=p\r\n"  -> PTT button pressed
 *       "+ptt=r\r\n"  -> PTT button released
 *
 * Add to CMakeLists.txt:
 *   find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Network Multimedia Bluetooth)
 *   ... target_link_libraries(applatry PRIVATE Qt6::Bluetooth)
 *   ... add SppPttBridge.cpp to qt_add_executable(applatry ...)
 *
 * Android manifest additions (AndroidManifest.xml):
 *   <uses-permission android:name="android.permission.BLUETOOTH" />
 *   <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
 *   <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
 */

#ifndef SppPttBridge_H
#define SppPttBridge_H

#include <QObject>
#include <QBluetoothSocket>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QVariantList>

class SppPttBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString pairedDeviceName READ pairedDeviceName NOTIFY pairedDeviceChanged)
    Q_PROPERTY(QString pairedDeviceAddress READ pairedDeviceAddress NOTIFY pairedDeviceChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY discoveredDevicesChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)

public:
    explicit SppPttBridge(QObject *parent = nullptr);
    ~SppPttBridge() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    QString status() const { return m_status; }
    QString pairedDeviceName() const { return m_deviceName; }
    QString pairedDeviceAddress() const { return m_deviceAddress; }
    bool isConnected() const;
    QVariantList discoveredDevices() const { return m_discoveredDevices; }
    bool isScanning() const { return m_scanning; }

    // Scan for nearby/paired Bluetooth devices so the user can pick the B02
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();

    // Persist + (re)connect to a chosen device, e.g. "B02_1234" / "AA:BB:CC:DD:EE:FF"
    Q_INVOKABLE void selectDevice(const QString &name, const QString &address,
                                  const QString &pressPattern = QString(),
                                  const QString &releasePattern = QString());

    // Manually (re)connect using the previously saved device
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void disconnectDevice();

    // Optional: lets the settings UI test the wiring without the real button
    Q_INVOKABLE void simulatePtt(bool pressed);
    Q_INVOKABLE void startLearning();
    Q_INVOKABLE void stopLearning();

signals:
    void enabledChanged();
    void statusChanged();
    void pairedDeviceChanged();
    void connectedChanged();
    void discoveredDevicesChanged();
    void scanningChanged();

    // Connect these to ReflectorClient::pttPressed()/pttReleased() in main.cpp
    void pttButtonPressed();
    void pttButtonReleased();
    void learningComplete(const QString &pressPattern, const QString &releasePattern);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QBluetoothSocket::SocketError error);
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onDiscoveryFinished();

private:
    void setStatus(const QString &status);
    void connectToSavedDevice();
    void doConnectSocket();
    void processLine(const QByteArray &line);

    bool m_enabled = false;
    bool m_scanning = false;
    bool    m_learning = false;
    QString m_status = QStringLiteral("Disabled");
    QString m_deviceName;
    QString m_deviceAddress;
    QString m_pressPattern;
    QString m_releasePattern;
    QString m_learnedPress;

    QBluetoothSocket *m_socket = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent = nullptr;
    QByteArray m_rxBuffer;
    QVariantList m_discoveredDevices;
};

#endif // SppPttBridge_H