#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <Worker.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    qmlRegisterType<Worker>("MyApp", 1, 0, "Worker");

    const QUrl url(QStringLiteral("qrc:/processEvent/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
