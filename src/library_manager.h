#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QUrl>
#include <QPointer>
#include "thumbnail_generator.h"

class LibraryManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        RatingRole,
        KindRole,
        ProgressRole,
        PosterUrlRole,
        DurationRole,
        ImdbIdRole,
        FolderPathRole,
        EntryIdRole,
        PositionMsRole,
        DurationMsRole,
    };

    explicit LibraryManager(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void         addMovie(const QUrl &fileUrl);
    Q_INVOKABLE void         rescanShow(int entryId);
    Q_INVOKABLE void         addShow(const QUrl &folderUrl);
    Q_INVOKABLE void         removeAt(int row);
    Q_INVOKABLE void         moveEntry(int fromRow, int toRow);
    Q_INVOKABLE QVariantList episodesForShow(int entryId) const;

    Q_INVOKABLE void updateEpisodeProgress(int entryId, int season, int episode,
                                           qint64 positionMs, qint64 durationMs);
    Q_INVOKABLE void updateMovieProgress(int entryId,
                                         qint64 positionMs, qint64 durationMs);

    Q_INVOKABLE void    saveSetting(const QString &key, const QString &value);
    Q_INVOKABLE QString getSetting(const QString &key,
                                   const QString &defaultValue = "") const;

    Q_INVOKABLE QVariantMap nextEpisode(int entryId, int season, int episode) const;

    // progress bar hover  thumbnails
    Q_INVOKABLE QString scrubCacheDir(int entryId, int season, int episode) const;
    Q_INVOKABLE void    ensureScrubThumbnails(int entryId, int season, int episode,
                                           const QString &filePath, qint64 durationMs);

    //  Remember manually added subtitles
    Q_INVOKABLE QString episodeSubtitle(int entryId, int season, int episode) const;
    Q_INVOKABLE void    setEpisodeSubtitle(int entryId, int season, int episode,
                                        const QString &subtitlePath);
    Q_INVOKABLE QString movieSubtitle(int entryId) const;
    Q_INVOKABLE void    setMovieSubtitle(int entryId, const QString &subtitlePath);

signals:
    void movieAdded(const QString &title);
    void showAdded(const QString &title);
    void itemRemoved(const QString &title);
    void duplicateMovie(const QString &title);
    void duplicateShow(const QString &title);
    void episodesUpdated(int entryId);
    void scrubThumbnailsReady(int entryId, int season, int episode,
                              int frameCount, int intervalSec);

private slots:
    void onThumbnailReady(int showId, int season, int episode,
                          const QString &thumbnailPath);

private:
    struct EpisodeEntry {
        int     season        = 1;
        int     episode       = 1;
        QString title;
        QString filePath;
        QString imdbRating;
        QString duration;
        QString thumbnailPath;
        QString subtitlePath;
        qint64  positionMs    = 0;
        qint64  durationMs    = 0;
    };

    struct LibraryEntry {
        int     id            = 0;
        QString title;
        double  rating        = 0.0;
        QString kind;
        double  progress      = 0.0;
        QString filePath;
        QString posterUrl;
        QString duration;
        QString imdbId;
        QString subtitlePath;
        int     sortOrder     = 0;
        qint64  positionMs    = 0;
        qint64  durationMs    = 0;
        mutable bool episodesLoaded = false;
        mutable QList<EpisodeEntry> episodes;
    };

    QList<LibraryEntry>    m_entries;
    int                    m_nextId = 1;
    QNetworkAccessManager *m_network;
    QSqlDatabase           m_db;
    ThumbnailGenerator    *m_thumbnailGenerator;
    QPointer<QProcess>     m_scrubProcess;

    void ensureEpisodesLoaded(const LibraryEntry &entry) const;
    QString guessTitleFromFileName(const QString &fileName) const;
    void    fetchMetadata(int entryId, const QString &searchTitle);
    void    applyMetadata(int entryId, const QJsonObject &obj);
    void    fetchShowEpisodes(int entryId, const QString &imdbId, int season);
    int     rowForId(int entryId) const;

    QList<EpisodeEntry> scanShowFolder(const QString &folderPath) const;
    int parseSeasonEpisode(const QString &fileName,
                           int &outSeason, int &outEpisode) const;

    QString thumbnailOutputPath(int showId, int season, int episode) const;
    void    queueMissingThumbnails(const LibraryEntry &entry);

    // Poster caching
    QString posterOutputPath(int entryId) const;
    void    downloadPoster(int entryId, const QString &remoteUrl);
    void    generatePosterFromVideo(int entryId);
    QString ffmpegPath() const;

    void openDatabase();
    void createTablesIfNeeded();
    void migrateAddThumbnailColumnIfNeeded();
    void migrateAddSortOrderColumnIfNeeded();
    void migrateAddMovieProgressColumnsIfNeeded();
    void migrateAddSubtitleColumnsIfNeeded();
    void migrateLocalizeRemotePostersIfNeeded();
    void loadFromDatabase();
    void saveEntryToDatabase(const LibraryEntry &entry);
    void saveEpisodeToDatabase(int showId, const EpisodeEntry &ep);
    void updateEntryMetadataInDatabase(const LibraryEntry &entry);
    void updateEpisodeMetadataInDatabase(int showId, const EpisodeEntry &ep);
    void updateEpisodeThumbnailInDatabase(int showId, int season, int episode,
                                          const QString &thumbnailPath);
    void updateEpisodeSubtitleInDatabase(int showId, int season, int episode,
                                         const QString &subtitlePath);
    void updateEntrySubtitleInDatabase(int entryId, const QString &subtitlePath);
    void updateEpisodePositionInDatabase(int showId, int season, int episode,
                                         qint64 positionMs, qint64 durationMs);
    void updateEntryPositionInDatabase(int entryId,
                                       qint64 positionMs, qint64 durationMs);
    void deleteEntryFromDatabase(int entryId);
    void persistSortOrder();
    void updateSortOrderInDatabase(int entryId, int sortOrder);
    void updateEntryProgressInDatabase(int entryId, double progress);
    void updateEntryPosterInDatabase(int entryId, const QString &posterPath);
};