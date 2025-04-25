#include "Worker.h"
#include <QtConcurrent/QtConcurrent>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QMutex>
#include <QThreadPool>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>

QQueue<QPair<QUrl, int>> Worker::queue;
bool Worker::isProcessing = false;

Worker::Worker(QObject *parent) : QObject(parent), m_progress(0.0), m_imagesLoaded(0) {
    watcher = new QFutureWatcher<void>(this);
    networkManager = new QNetworkAccessManager(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, &Worker::taskFinished);

    m_urls.clear();
    for (int i = 0; i < 16; ++i) {
        m_urls << QString("https://images.pexels.com/photos/417074/pexels-photo-417074.jpeg?auto=compress&cs=tinysrgb&w=1260&h=750&dpr=2");
    }

    qDebug() << "Initializing URLs: count =" << m_urls.size();
    for (const QString &url : m_urls) {
        QUrl qurl(url);
        if (!qurl.isValid()) {
            qDebug() << "Invalid URL:" << url;
        } else {
            qDebug() << "Valid URL:" << url;
        }
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(tempDir);
    if (!dir.exists()) {
        qDebug() << "Temp directory does not exist:" << tempDir;
        if (!dir.mkpath(tempDir)) {
            qDebug() << "Failed to create temp directory:" << tempDir;
        }
    } else {
        qDebug() << "Temp directory is writable:" << tempDir;
    }
}

Worker::~Worker() {
    watcher->cancel();
    watcher->waitForFinished();
}

void Worker::startTask() {
    m_progress = 0.0;
    m_imagesLoaded = 0;
    m_imagePaths.clear();
    queue.clear();
    isProcessing = false;
    emit progressChanged();
    emit imagePathsChanged();

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(tempDir);
    dir.setNameFilters(QStringList() << "*.png" << "*.jpg" << "*.jpeg");
    // qDebug() << "Clearing old images in:" << tempDir;
    for (const QString &file : dir.entryList()) {
        if (dir.remove(file)) {
            qDebug() << "Removed old file:" << file;
        } else {
            qDebug() << "Failed to remove file:" << file;
        }
    }

    QThreadPool::globalInstance()->setMaxThreadCount(4);

    QList<QPair<QString, int>> tasks;
    for (int i = 0; i < m_urls.size(); ++i) {
        QUrl qurl(m_urls[i]);
        if (qurl.isValid()) {
            tasks.append(QPair<QString, int>(m_urls[i], i));
            // qDebug() << "Added task for URL:" << m_urls[i] << "at index:" << i;
        } else {
            emit errorOccurred(QString("Invalid URL at index %1: %2").arg(i).arg(m_urls[i]));
            qDebug() << "Invalid URL skipped:" << m_urls[i];
        }
    }

    if (tasks.isEmpty()) {
        emit errorOccurred("No valid URLs to download");
        emit taskFinished();
        qDebug() << "No valid tasks to process";
        return;
    }

    // Limit threadPool
    const int maxConcurrent = 8;
    QList<QFuture<void>> futures;
    for (int i = 0; i < tasks.size(); ++i) {
        const QString urlStr = tasks[i].first;
        const int index = tasks[i].second;
        QFuture<void> future = QtConcurrent::run([this, urlStr, index]() {
            QUrl url(urlStr);
            qDebug() << "Running task for URL:" << urlStr << "index:" << index
                     << "isValid:" << url.isValid() << "scheme:" << url.scheme()
                     << "canceled:" << watcher->isCanceled();
            if (!watcher->isCanceled()) {
                QMetaObject::invokeMethod(this, [this, url, index]() {
                    downloadImageInMainThread(url, index);
                }, Qt::QueuedConnection);
            } else {
                qDebug() << "Task canceled for URL:" << urlStr << "index:" << index;
            }
        });

        futures.append(future);
        if (futures.size() >= maxConcurrent) {
            QEventLoop loop;
            for (auto &f : futures) {
                connect(watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
                watcher->setFuture(f);
                loop.exec();
            }
            futures.clear();
        }
    }

    QTimer::singleShot(0, this, [this, futures]() mutable {
        QEventLoop loop;
        for (auto &f : futures) {
            connect(watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
            watcher->setFuture(f);
            loop.exec();
        }
        if (!watcher->isCanceled()) {
            qDebug() << "All futures completed, checking queue";
            //  queue handler done
            QMetaObject::invokeMethod(this, [this]() {
                processQueue();
            }, Qt::QueuedConnection);
        }
    });
}

void Worker::cancelTask() {
    watcher->cancel();
    QMetaObject::invokeMethod(this, [this]() {
        m_progress = 0.0;
        m_imagesLoaded = 0;
        m_imagePaths.clear();
        queue.clear();
        isProcessing = false;
        emit progressChanged();
        emit imagePathsChanged();
        qDebug() << "Task canceled, reset state";
    }, Qt::QueuedConnection);
}

void Worker::downloadImage(const QUrl &url, int index) {
    qDebug() << "downloadImage called for URL:" << url.toString() << "index:" << index;
    downloadImageInMainThread(url, index);
}

void Worker::downloadImageInMainThread(const QUrl &url, int index) {
    qDebug() << "downloadImageInMainThread called for URL:" << url.toString() << "index:" << index
             << "queue size:" << queue.size();
    if (watcher->isCanceled()) {
        qDebug() << "Download canceled for URL:" << url.toString();
        return;
    }

    queue.enqueue({url, index});
    if (!isProcessing) {
        isProcessing = true;
        processQueue();
    }
}

void Worker::processQueue() {
    if (queue.isEmpty()) {
        isProcessing = false;
        if (m_imagesLoaded == m_urls.size()) {
            emit taskFinished();
            qDebug() << "Queue empty, task finished";
        }
        return;
    }
    auto task = queue.dequeue();
    QUrl url = task.first;
    int index = task.second;

    qDebug() << "Processing queue for URL:" << url.toString() << "index:" << index
             << "remaining queue size:" << queue.size();
    if (!url.isValid()) {
        emit errorOccurred(QString("Invalid URL in queue: %1").arg(url.toString()));
        processQueue();
        return;
    }

    QNetworkReply *reply = networkManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, index, url]() {
        qDebug() << "QNetworkReply finished for URL:" << url.toString() << "index:" << index
                 << "error:" << reply->error();
        if (watcher->isCanceled()) {
            reply->deleteLater();
            qDebug() << "Download canceled during reply for URL:" << url.toString();
            processQueue();
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString filePath = QString("%1/image_%2.jpg").arg(tempDir).arg(index);
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                qDebug() << "Saved image to:" << filePath;


                QFileInfo fileInfo(filePath);
                if (fileInfo.exists() && fileInfo.size() > 0) {
                    // update path and progress
                    static QMutex mutex;
                    QMutexLocker locker(&mutex);
                    addImagePath(filePath);
                    updateProgress(m_imagesLoaded, m_urls.size());
                } else {
                    emit errorOccurred(QString("Saved file is invalid or empty: %1").arg(filePath));
                    qDebug() << "Invalid or empty file:" << filePath;
                }
            } else {
                emit errorOccurred(QString("Failed to write file: %1").arg(filePath));
                qDebug() << "Failed to write file:" << filePath << ", error:" << file.errorString();
            }
        } else {
            emit errorOccurred(QString("Error downloading %1: %2").arg(url.toString()).arg(reply->errorString()));
            qDebug() << "Download error for URL:" << url.toString() << ", error:" << reply->errorString();
        }
        reply->deleteLater();
        processQueue();
    });
}

void Worker::updateProgress(int loaded, int total) {
    m_imagesLoaded = loaded + 1;
    m_progress = static_cast<float>(m_imagesLoaded) / total;
    emit progressChanged();
    qDebug() << "Progress updated:" << m_progress * 100 << "% (" << m_imagesLoaded << "/" << total << ")";
}

void Worker::addImagePath(const QString &path) {
    static int batchSize = 0;
    m_imagePaths.append(path);
    if (++batchSize >= 10 || m_imagesLoaded == m_urls.size()) {
        emit imagePathsChanged();
        batchSize = 0;
    }
    qDebug() << "Added image path:" << path;
}
