#pragma once
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>

class ThumbnailGenerator : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailGenerator(QObject *parent = nullptr);

    struct Job {
        int     showId  = 0;
        int     season  = 1;
        int     episode = 1;
        QString videoPath;
        QString outputPath;
    };

    void enqueue(const Job &job);

signals:
    void thumbnailReady(int showId, int season, int episode, const QString &thumbnailPath);
    void thumbnailFailed(int showId, int season, int episode);

private:
    void startNext();
    QString ffmpegPath() const;

    QQueue<Job> m_queue;
    QProcess   *m_process = nullptr;
    Job         m_currentJob;
    bool        m_busy = false;
};