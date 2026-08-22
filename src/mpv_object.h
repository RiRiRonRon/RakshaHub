#pragma once
#include <QQuickFramebufferObject>
#include <QTimer>
#include <QVariantList>
#include <QWindow>
#include <mpv/client.h>
#include <mpv/render_gl.h>


class MpvObject : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvPlayer)
    Q_PROPERTY(bool         playing        READ isPlaying      NOTIFY playingChanged)
    Q_PROPERTY(qint64       position       READ position       NOTIFY positionChanged)
    Q_PROPERTY(qint64       duration       READ duration       NOTIFY durationChanged)
    Q_PROPERTY(int          volume         READ volume         WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool         hasVideo       READ hasVideo       NOTIFY hasVideoChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(double       speed          READ speed          WRITE setSpeed NOTIFY speedChanged)
public:
    explicit MpvObject(QQuickItem *parent = nullptr);
    ~MpvObject() override;
    Renderer *createRenderer() const override;

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;

public:

    mpv_handle         *mpvHandle()     const { return m_mpv; }
    mpv_render_context *renderContext() const { return m_renderContext; }

    void setRenderContext(mpv_render_context *ctx)
    {
        m_renderContext = ctx;
        QMetaObject::invokeMethod(this, "renderReady", Qt::QueuedConnection);
    }
    static void mpvUpdateCallback(void *ctx);
    // stats  getters
    bool         isPlaying()      const { return m_playing; }
    qint64       position()       const { return m_position; }
    qint64       duration()       const { return m_duration; }
    int          volume()         const { return m_volume; }
    bool         hasVideo()       const { return m_hasVideo; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    double       speed()          const { return m_speed; }
public slots:
    void play(const QString &filePath, qint64 startMs = 0);
    void pause();
    void resume();
    void togglePause();
    void seek(qint64 ms);
    void stop();
    void setVolume(int vol);
    void setSubtitleTrack(int trackId);

    void setLowMemoryMode(bool on);

    void setSpeed(double s);
    void addSubtitleFile(const QString &filePath);
    void setSubtitleFontSize(double px);


signals:
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void hasVideoChanged();
    void subtitleTracksChanged();
    void speedChanged();
    void renderReady();
    void endReached(qint64 finalPositionMs);
    void stopped(qint64 finalPositionMs);
private slots:
    void pollMpv();
    void doUpdate();
    void onWindowActiveChanged();
    void onWindowVisibilityChanged(QWindow::Visibility visibility);
private:
    void handleMpvEvent(mpv_event *event);
    void refreshSubtitleTracks();
    mpv_handle         *m_mpv           = nullptr;
    mpv_render_context *m_renderContext = nullptr;
    bool         m_playing     = false;
    qint64       m_position    = 0;
    qint64       m_duration    = 0;
    int          m_volume      = 100;
    bool         m_hasVideo    = false;
    double       m_speed       = 1.0;
    qint64       m_startMs     = 0;
    bool         m_seekPending = false;
    QVariantList m_subtitleTracks;
    QTimer      *m_pollTimer = nullptr;
};