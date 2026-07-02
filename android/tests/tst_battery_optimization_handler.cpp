#include <QtTest>

#include <QCoreApplication>
#include <QNetworkReply>
#include <QSignalSpy>

#include "BatteryOptimizationHandler.h"

class FakeNetworkReply final : public QNetworkReply
{
    Q_OBJECT

public:
    explicit FakeNetworkReply(const QByteArray &payload,
                              NetworkError error = QNetworkReply::NoError,
                              const QString &errorText = QString(),
                              QObject *parent = nullptr)
        : QNetworkReply(parent)
        , m_payload(payload)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setHeader(QNetworkRequest::ContentLengthHeader, QVariant::fromValue(m_payload.size()));
        if (error != QNetworkReply::NoError) {
            setError(error, errorText);
        }
        setFinished(true);
    }

    void abort() override {}

    bool isSequential() const override
    {
        return true;
    }

    qint64 bytesAvailable() const override
    {
        return (m_payload.size() - m_offset) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (m_offset >= m_payload.size()) {
            return -1;
        }

        const qint64 bytesToRead = qMin(maxSize, m_payload.size() - m_offset);
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(bytesToRead));
        m_offset += bytesToRead;
        return bytesToRead;
    }

private:
    QByteArray m_payload;
    qint64 m_offset = 0;
};

class BatteryOptimizationHandlerTest : public QObject
{
    Q_OBJECT

private slots:
    void formatsManufacturerSpecificInstructions();
    void fallsBackToGenericInstructionsWhenSolutionMissing();
    void fallsBackToGenericInstructionsOnNetworkError();
    void requestInstructionsIsNoopOnDesktop();
};

void BatteryOptimizationHandlerTest::formatsManufacturerSpecificInstructions()
{
    BatteryOptimizationHandler handler;
    QSignalSpy spy(&handler, &BatteryOptimizationHandler::showBatteryOptimizationInstructions);
    QVERIFY(spy.isValid());

    auto *reply = new FakeNetworkReply(
        R"({"name":"Samsung","user_solution":"Open [Your app] settings and keep Your app alive <img src='step.png'>"})");

    handler.onApiResult(reply);
    QCOMPARE(spy.count(), 1);

    const QString instructions = spy.takeFirst().at(0).toString();
    QVERIFY(instructions.contains(QStringLiteral("Battery Optimization Setup for Samsung Devices")));
    QVERIFY(instructions.contains(QStringLiteral("Open Latry settings")));
    QVERIFY(!instructions.contains(QStringLiteral("[Your app]")));
    QVERIFY(!instructions.contains(QStringLiteral("Your app")));
    QVERIFY(instructions.contains(QStringLiteral("max-width: 100%")));
    QVERIFY(instructions.contains(QStringLiteral("dontkillmyapp.com")));

    QCoreApplication::sendPostedEvents(reply, QEvent::DeferredDelete);
}

void BatteryOptimizationHandlerTest::fallsBackToGenericInstructionsWhenSolutionMissing()
{
    BatteryOptimizationHandler handler;
    QSignalSpy spy(&handler, &BatteryOptimizationHandler::showBatteryOptimizationInstructions);
    QVERIFY(spy.isValid());

    auto *reply = new FakeNetworkReply(R"({"name":"Samsung","user_solution":""})");

    handler.onApiResult(reply);
    QCOMPARE(spy.count(), 1);

    const QString instructions = spy.takeFirst().at(0).toString();
    QVERIFY(instructions.contains(QStringLiteral("Battery Optimization Setup for Unknown Devices")));
    QVERIFY(instructions.contains(QStringLiteral("Disable Battery Optimization")));
    QVERIFY(instructions.contains(QStringLiteral("Allow Background Activity")));

    QCoreApplication::sendPostedEvents(reply, QEvent::DeferredDelete);
}

void BatteryOptimizationHandlerTest::fallsBackToGenericInstructionsOnNetworkError()
{
    BatteryOptimizationHandler handler;
    QSignalSpy spy(&handler, &BatteryOptimizationHandler::showBatteryOptimizationInstructions);
    QVERIFY(spy.isValid());

    auto *reply = new FakeNetworkReply(
        QByteArray(),
        QNetworkReply::TimeoutError,
        QStringLiteral("timed out"));

    handler.onApiResult(reply);
    QCOMPARE(spy.count(), 1);

    const QString instructions = spy.takeFirst().at(0).toString();
    QVERIFY(instructions.contains(QStringLiteral("Battery Optimization Setup for Unknown Devices")));
    QVERIFY(instructions.contains(QStringLiteral("These are general instructions for Android devices")));

    QCoreApplication::sendPostedEvents(reply, QEvent::DeferredDelete);
}

void BatteryOptimizationHandlerTest::requestInstructionsIsNoopOnDesktop()
{
    BatteryOptimizationHandler handler;
    QSignalSpy spy(&handler, &BatteryOptimizationHandler::showBatteryOptimizationInstructions);
    QVERIFY(spy.isValid());

    handler.requestBatteryOptimizationInstructions();

    QCOMPARE(spy.count(), 0);
}

QTEST_GUILESS_MAIN(BatteryOptimizationHandlerTest)

#include "tst_battery_optimization_handler.moc"
