#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QQueue>

class Worker : public QObject {
    Q_OBJECT
    Q_PROPERTY(float progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QStringList imagePaths READ imagePaths NOTIFY imagePathsChanged)

public:
    explicit Worker(QObject *parent = nullptr);
    ~Worker();

    float progress() const { return m_progress; }
    QStringList imagePaths() const { return m_imagePaths; }

public slots:
    void startTask();
    void cancelTask();
    void downloadImageInMainThread(const QUrl &url, int index);

signals:
    void progressChanged();
    void imagePathsChanged();
    void taskFinished();
    void errorOccurred(const QString &error);

private:
    void downloadImage(const QUrl &url, int index);
    void updateProgress(int loaded, int total);
    void addImagePath(const QString &path);
    void processQueue();

private:
    float m_progress;
    QStringList m_imagePaths;
    QFutureWatcher<void> *watcher;
    QNetworkAccessManager *networkManager;
    QList<QString> m_urls;
    int m_imagesLoaded;

    static QQueue<QPair<QUrl, int>> queue;
    static bool isProcessing;
};

#endif // WORKER_H
