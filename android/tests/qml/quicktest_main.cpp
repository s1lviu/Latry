#include <QtQuickTest/quicktest.h>

#include <QQmlEngine>

class LatryQuickTestSetup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QStringLiteral(LATRY_QML_IMPORT_ROOT));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(latry_qml, LatryQuickTestSetup)

#include "quicktest_main.moc"
