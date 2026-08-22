#include "library_manager.h"
#include "thumbnail_generator.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrlQuery>
#include <algorithm>

#include "secrets.h" ///api key

#ifdef QT_DEBUG
static const QString kDevFfmpegPath = QStringLiteral("D:/ffmpeg/ffmpeg.exe");
#endif

static const QStringList kVideoExtensions = {
    "mp4", "mkv", "avi", "mov", "wmv", "m4v", "flv", "webm"
};

LibraryManager::LibraryManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_thumbnailGenerator(new ThumbnailGenerator(this))
{
    connect(m_thumbnailGenerator, &ThumbnailGenerator::thumbnailReady,
            this, &LibraryManager::onThumbnailReady);

    openDatabase();
    createTablesIfNeeded();
    migrateAddThumbnailColumnIfNeeded();
    migrateAddSortOrderColumnIfNeeded();
    migrateAddMovieProgressColumnsIfNeeded();
    migrateAddSubtitleColumnsIfNeeded();
    loadFromDatabase();
    migrateLocalizeRemotePostersIfNeeded();
}

int LibraryManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.count();
}

QVariant LibraryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.count())
        return {};
    const LibraryEntry &e = m_entries.at(index.row());
    switch (role) {
    case TitleRole:      return e.title;
    case RatingRole:     return e.rating;
    case KindRole:       return e.kind;
    case ProgressRole:   return e.progress;
    case PosterUrlRole:  return e.posterUrl;
    case DurationRole:   return e.duration;
    case ImdbIdRole:     return e.imdbId;
    case FolderPathRole: return e.filePath;
    case EntryIdRole:    return e.id;
    case PositionMsRole: return e.positionMs;
    case DurationMsRole: return e.durationMs;
    default:             return {};
    }
}

QHash<int, QByteArray> LibraryManager::roleNames() const
{
    return {
             { TitleRole,      "title"      },
             { RatingRole,     "rating"     },
             { KindRole,       "kind"       },
             { ProgressRole,   "progress"   },
             { PosterUrlRole,  "posterUrl"  },
             { DurationRole,   "duration"   },
             { ImdbIdRole,     "imdbId"     },
             { FolderPathRole, "folderPath" },
             { EntryIdRole,    "entryId"    },
             { PositionMsRole, "positionMs" },
             { DurationMsRole, "durationMs" },
             };
}

void LibraryManager::ensureEpisodesLoaded(const LibraryEntry &e) const
{
    if (e.episodesLoaded) return;
    e.episodesLoaded = true;
    e.episodes.clear();

    if (e.kind != "Show") return;

    QSqlQuery eq(m_db);
    eq.prepare("SELECT season,episode,title,file_path,imdb_rating,"
               "duration,position_ms,duration_ms,thumbnail_path,subtitle_path FROM episodes "
               "WHERE show_id = :sid ORDER BY season,episode");
    eq.bindValue(":sid", e.id);
    if (eq.exec()) {
        while (eq.next()) {
            EpisodeEntry ep;
            ep.season        = eq.value(0).toInt();
            ep.episode       = eq.value(1).toInt();
            ep.title         = eq.value(2).toString();
            ep.filePath      = eq.value(3).toString();
            ep.imdbRating    = eq.value(4).toString();
            ep.duration      = eq.value(5).toString();
            ep.positionMs    = eq.value(6).toLongLong();
            ep.durationMs    = eq.value(7).toLongLong();
            ep.thumbnailPath = eq.value(8).toString();
            ep.subtitlePath  = eq.value(9).toString();
            e.episodes.append(ep);
        }
    }
}

void LibraryManager::addMovie(const QUrl &fileUrl)
{
    const QString filePath = fileUrl.toLocalFile();
    for (const LibraryEntry &e : std::as_const(m_entries)) {
        if (e.filePath.compare(filePath, Qt::CaseInsensitive) == 0) {
            emit duplicateMovie(e.title);
            return;
        }
    }

    const QFileInfo info(filePath);
    const QString guessedTitle = guessTitleFromFileName(info.completeBaseName());
    const int entryId = m_nextId++;
    const int newRow  = m_entries.count();

    LibraryEntry entry;
    entry.id        = entryId;
    entry.title     = guessedTitle;
    entry.kind      = "Movie";
    entry.filePath  = filePath;
    entry.sortOrder = newRow;

    beginInsertRows(QModelIndex(), newRow, newRow);
    m_entries.append(entry);
    endInsertRows();

    saveEntryToDatabase(entry);
    emit movieAdded(guessedTitle);
    fetchMetadata(entryId, guessedTitle);
}

void LibraryManager::addShow(const QUrl &folderUrl)
{
    const QString folderPath = folderUrl.toLocalFile();
    for (const LibraryEntry &e : std::as_const(m_entries)) {
        if (e.kind == "Show" &&
            e.filePath.compare(folderPath, Qt::CaseInsensitive) == 0) {
            emit duplicateShow(e.title);
            return;
        }
    }

    QList<EpisodeEntry> episodes = scanShowFolder(folderPath);
    if (episodes.isEmpty()) {
        qDebug() << "No video files found in:" << folderPath;
        return;
    }

    const QFileInfo folderInfo(folderPath);
    const QString guessedTitle = guessTitleFromFileName(folderInfo.fileName());
    const int entryId = m_nextId++;
    const int newRow  = m_entries.count();

    LibraryEntry entry;
    entry.id             = entryId;
    entry.title          = guessedTitle;
    entry.kind           = "Show";
    entry.filePath       = folderPath;
    entry.episodes       = episodes;
    entry.episodesLoaded = true;
    entry.sortOrder      = newRow;

    beginInsertRows(QModelIndex(), newRow, newRow);
    m_entries.append(entry);
    endInsertRows();

    saveEntryToDatabase(entry);
    for (const EpisodeEntry &ep : std::as_const(episodes))
        saveEpisodeToDatabase(entryId, ep);

    emit showAdded(guessedTitle);
    fetchMetadata(entryId, guessedTitle);
    queueMissingThumbnails(entry);
}

void LibraryManager::rescanShow(int entryId)
{
    const int row = rowForId(entryId);
    if (row < 0) return;
    LibraryEntry &entry = m_entries[row];
    if (entry.kind != "Show" || entry.filePath.isEmpty()) return;

    ensureEpisodesLoaded(entry);

    const QList<EpisodeEntry> found = scanShowFolder(entry.filePath);
    bool changed = false;
    QSet<int> newSeasons;

    for (const EpisodeEntry &scanned : found) {
        bool exists = false;
        for (const EpisodeEntry &existing : std::as_const(entry.episodes)) {
            if (existing.season  == scanned.season &&
                existing.episode == scanned.episode) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            entry.episodes.append(scanned);
            saveEpisodeToDatabase(entryId, scanned);
            newSeasons.insert(scanned.season);
            changed = true;
        }
    }

    if (changed) {
        std::sort(entry.episodes.begin(), entry.episodes.end(),
                  [](const EpisodeEntry &a, const EpisodeEntry &b) {
                      if (a.season != b.season) return a.season < b.season;
                      return a.episode < b.episode;
                  });

        queueMissingThumbnails(entry);

        if (!entry.imdbId.isEmpty()) {
            for (int season : std::as_const(newSeasons))
                fetchShowEpisodes(entryId, entry.imdbId, season);
        }

        emit episodesUpdated(entryId);
    }
}

void LibraryManager::moveEntry(int fromRow, int toRow)
{
    if (fromRow == toRow) return;
    if (fromRow < 0 || fromRow >= m_entries.count()) return;
    if (toRow   < 0 || toRow   >= m_entries.count()) return;

    const int dest = toRow > fromRow ? toRow + 1 : toRow;
    if (!beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), dest))
        return;
    m_entries.move(fromRow, toRow);
    endMoveRows();
    persistSortOrder();
}

void LibraryManager::updateEpisodeProgress(int entryId, int season, int episode,
                                           qint64 positionMs, qint64 durationMs)
{
    const int row = rowForId(entryId);
    if (row < 0) return;

    LibraryEntry &e = m_entries[row];
    ensureEpisodesLoaded(e);

    for (EpisodeEntry &ep : e.episodes) {
        if (ep.season == season && ep.episode == episode) {
            ep.positionMs = positionMs;
            ep.durationMs = durationMs;
            break;
        }
    }

    if (e.episodes.count() == 1) {
        const EpisodeEntry &ep = e.episodes.first();
        e.progress = ep.durationMs > 0
                         ? qBound(0.0,
                                  static_cast<double>(ep.positionMs) / ep.durationMs,
                                  0.96)
                         : 0.0;
    } else {
        int watched = 0;
        for (const EpisodeEntry &ep : e.episodes)
            if (ep.durationMs > 0 && ep.positionMs > 0) ++watched;
        if (!e.episodes.isEmpty())
            e.progress = static_cast<double>(watched) / e.episodes.count();
    }

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);

    updateEpisodePositionInDatabase(entryId, season, episode, positionMs, durationMs);
    updateEntryProgressInDatabase(entryId, e.progress);
    emit episodesUpdated(entryId);
}

void LibraryManager::updateMovieProgress(int entryId,
                                         qint64 positionMs, qint64 durationMs)
{
    const int row = rowForId(entryId);
    if (row < 0) return;

    LibraryEntry &e = m_entries[row];
    e.positionMs = positionMs;
    e.durationMs = durationMs;
    e.progress   = (durationMs > 0)
                     ? qBound(0.0,
                              static_cast<double>(positionMs) / durationMs,
                              1.0)
                     : 0.0;

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);

    updateEntryPositionInDatabase(entryId, positionMs, durationMs);
    updateEntryProgressInDatabase(entryId, e.progress);
}

QList<LibraryManager::EpisodeEntry> LibraryManager::scanShowFolder(
    const QString &folderPath) const
{
    QList<EpisodeEntry> result;
    QDirIterator it(folderPath, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (!fi.isFile()) continue;
        if (!kVideoExtensions.contains(fi.suffix().toLower())) continue;

        int season = -1, episode = -1;
        if (parseSeasonEpisode(fi.fileName(), season, episode) < 0) {
            continue;
        }

        EpisodeEntry ep;
        ep.season   = season;
        ep.episode  = episode;
        ep.filePath = path;
        ep.title    = QString("Episode %1").arg(episode);
        result.append(ep);
    }

    std::sort(result.begin(), result.end(),
              [](const EpisodeEntry &a, const EpisodeEntry &b) {
                  if (a.season != b.season) return a.season < b.season;
                  return a.episode < b.episode;
              });
    return result;
}

int LibraryManager::parseSeasonEpisode(const QString &fileName,
                                       int &outSeason, int &outEpisode) const
{
    static const QRegularExpression reSxE(
        R"([Ss](\d{1,2})[Ee](\d{1,2}))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reXx(R"(\b(\d{1,2})[xX](\d{2})\b)");

    QRegularExpressionMatch m = reSxE.match(fileName);
    if (!m.hasMatch()) m = reXx.match(fileName);
    if (!m.hasMatch()) return -1;

    outSeason  = m.captured(1).toInt();
    outEpisode = m.captured(2).toInt();
    return 0;
}

QVariantList LibraryManager::episodesForShow(int entryId) const
{
    const int row = rowForId(entryId);
    if (row < 0) return {};

    const LibraryEntry &entry = m_entries.at(row);
    ensureEpisodesLoaded(entry);

    QVariantList result;
    for (const EpisodeEntry &ep : entry.episodes) {
        QVariantMap m;
        m["season"]        = ep.season;
        m["episode"]       = ep.episode;
        m["title"]         = ep.title;
        m["filePath"]      = ep.filePath;
        m["rating"]        = ep.imdbRating;
        m["duration"]      = ep.duration;
        m["positionMs"]    = ep.positionMs;
        m["durationMs"]    = ep.durationMs;
        m["thumbnailPath"] = ep.thumbnailPath;
        result.append(m);
    }
    return result;
}

QVariantMap LibraryManager::nextEpisode(int entryId, int season, int episode) const
{
    const int row = rowForId(entryId);
    if (row < 0) return {{ "exists", false }};

    const LibraryEntry &entry = m_entries.at(row);
    if (entry.kind != "Show") return {{ "exists", false }};

    ensureEpisodesLoaded(entry);


    const EpisodeEntry *nextInSeason = nullptr;
    for (const EpisodeEntry &ep : entry.episodes) {
        if (ep.season == season && ep.episode > episode) {
            if (!nextInSeason || ep.episode < nextInSeason->episode)
                nextInSeason = &ep;
        }
    }

    if (nextInSeason) {
        return {
                 { "exists",       true                   },
                 { "isNextSeason", false                  },
                 { "filePath",     nextInSeason->filePath  },
                 { "season",       nextInSeason->season    },
                 { "episode",      nextInSeason->episode   },
                 { "title",        nextInSeason->title     },
                 { "positionMs",   nextInSeason->positionMs},
                 };
    }

    const EpisodeEntry *firstOfNextSeason = nullptr;
    for (const EpisodeEntry &ep : entry.episodes) {
        if (ep.season == season + 1) {
            if (!firstOfNextSeason || ep.episode < firstOfNextSeason->episode)
                firstOfNextSeason = &ep;
        }
    }

    if (firstOfNextSeason) {
        return {
                 { "exists",       true                          },
                 { "isNextSeason", true                          },
                 { "filePath",     firstOfNextSeason->filePath   },
                 { "season",       firstOfNextSeason->season     },
                 { "episode",      firstOfNextSeason->episode    },
                 { "title",        firstOfNextSeason->title      },
                 { "positionMs",   firstOfNextSeason->positionMs },
                 };
    }

    return {{ "exists", false }};
}

void LibraryManager::fetchMetadata(int entryId, const QString &searchTitle)
{
    const int row = rowForId(entryId);
    if (row < 0) return;
    const bool isShow = m_entries.at(row).kind == "Show";

    if (isShow) {
        QUrl url("https://www.omdbapi.com/");
        QUrlQuery query;
        query.addQueryItem("apikey", kOmdbApiKey);
        query.addQueryItem("s",      searchTitle);
        query.addQueryItem("type",   "series");
        url.setQuery(query);

        QNetworkReply *reply = m_network->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, entryId, searchTitle, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                generatePosterFromVideo(entryId);
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.value("Response").toString() != "True") {
                generatePosterFromVideo(entryId);
                return;
            }

            const QJsonArray results = obj.value("Search").toArray();
            if (results.isEmpty()) {
                generatePosterFromVideo(entryId);
                return;
            }

            QString bestId;
            for (const QJsonValue &v : results) {
                const QJsonObject r = v.toObject();
                if (r.value("Title").toString().compare(searchTitle, Qt::CaseInsensitive) == 0) {
                    bestId = r.value("imdbID").toString();
                    break;
                }
            }
            if (bestId.isEmpty())
                bestId = results.first().toObject().value("imdbID").toString();

            QUrl url2("https://www.omdbapi.com/");
            QUrlQuery q2;
            q2.addQueryItem("apikey", kOmdbApiKey);
            q2.addQueryItem("i",      bestId);
            url2.setQuery(q2);

            QNetworkReply *r2 = m_network->get(QNetworkRequest(url2));
            connect(r2, &QNetworkReply::finished, this, [this, entryId, r2]() {
                r2->deleteLater();
                if (r2->error() != QNetworkReply::NoError) {
                    generatePosterFromVideo(entryId);
                    return;
                }
                const QJsonObject obj2 = QJsonDocument::fromJson(r2->readAll()).object();
                if (obj2.value("Response").toString() != "True") {
                    generatePosterFromVideo(entryId);
                    return;
                }
                applyMetadata(entryId, obj2);
            });
        });
    } else {
        QUrl url("https://www.omdbapi.com/");
        QUrlQuery query;
        query.addQueryItem("apikey", kOmdbApiKey);
        query.addQueryItem("t",      searchTitle);
        query.addQueryItem("type",   "movie");
        url.setQuery(query);

        QNetworkReply *reply = m_network->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, entryId, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                generatePosterFromVideo(entryId);
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.value("Response").toString() != "True") {
                generatePosterFromVideo(entryId);
                return;
            }
            applyMetadata(entryId, obj);
        });
    }
}

void LibraryManager::applyMetadata(int entryId, const QJsonObject &obj)
{
    const int row = rowForId(entryId);
    if (row < 0) return;

    LibraryEntry &e = m_entries[row];
    e.title    = obj.value("Title").toString();
    e.rating   = obj.value("imdbRating").toString().toDouble();
    e.duration = obj.value("Runtime").toString();
    e.imdbId   = obj.value("imdbID").toString();

    const QString remotePoster = obj.value("Poster").toString();

    updateEntryMetadataInDatabase(e);
    emit dataChanged(index(row), index(row));

    if (!remotePoster.isEmpty() && remotePoster != "N/A")
        downloadPoster(entryId, remotePoster);
    else
        generatePosterFromVideo(entryId);

    if (e.kind == "Show" && !e.imdbId.isEmpty()) {
        ensureEpisodesLoaded(e);
        QSet<int> seasons;
        for (const EpisodeEntry &ep : std::as_const(e.episodes))
            seasons.insert(ep.season);
        for (int s : std::as_const(seasons))
            fetchShowEpisodes(entryId, e.imdbId, s);
    }
}

void LibraryManager::fetchShowEpisodes(int entryId, const QString &imdbId, int season)
{
    QUrl url("https://www.omdbapi.com/");
    QUrlQuery query;
    query.addQueryItem("apikey", kOmdbApiKey);
    query.addQueryItem("i",      imdbId);
    query.addQueryItem("Season", QString::number(season));
    url.setQuery(query);

    QNetworkReply *reply = m_network->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, entryId, season, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (obj.value("Response").toString() != "True") return;

        const int row = rowForId(entryId);
        if (row < 0) return;

        LibraryEntry &e = m_entries[row];
        ensureEpisodesLoaded(e);

        for (const QJsonValue &val : obj.value("Episodes").toArray()) {
            const QJsonObject ep  = val.toObject();
            const int epNum = ep.value("Episode").toString().toInt();
            for (EpisodeEntry &localEp : e.episodes) {
                if (localEp.season == season && localEp.episode == epNum) {
                    localEp.title      = ep.value("Title").toString();
                    localEp.imdbRating = ep.value("imdbRating").toString();
                    updateEpisodeMetadataInDatabase(entryId, localEp);
                    break;
                }
            }
        }
        emit dataChanged(index(row), index(row));
        emit episodesUpdated(entryId);
    });
}

QString LibraryManager::thumbnailOutputPath(int showId, int season, int episode) const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QString("%1/thumbnails/%2_S%3E%4.jpg")
        .arg(dataDir).arg(showId)
        .arg(season,  2, 10, QChar('0'))
        .arg(episode, 2, 10, QChar('0'));
}

void LibraryManager::queueMissingThumbnails(const LibraryEntry &entry)
{
    if (entry.kind != "Show") return;
    ensureEpisodesLoaded(entry);

    for (const EpisodeEntry &ep : entry.episodes) {
        if (!ep.thumbnailPath.isEmpty() && QFileInfo::exists(ep.thumbnailPath))
            continue;
        ThumbnailGenerator::Job job;
        job.showId     = entry.id;
        job.season     = ep.season;
        job.episode    = ep.episode;
        job.videoPath  = ep.filePath;
        job.outputPath = thumbnailOutputPath(entry.id, ep.season, ep.episode);
        m_thumbnailGenerator->enqueue(job);
    }
}

void LibraryManager::onThumbnailReady(int showId, int season, int episode, const QString &thumbnailPath)
{
    const int row = rowForId(showId);
    if (row < 0) return;

    ensureEpisodesLoaded(m_entries[row]);

    for (EpisodeEntry &ep : m_entries[row].episodes) {
        if (ep.season == season && ep.episode == episode) {
            ep.thumbnailPath = thumbnailPath;
            break;
        }
    }

    updateEpisodeThumbnailInDatabase(showId, season, episode, thumbnailPath);
    emit episodesUpdated(showId);
}

QString LibraryManager::posterOutputPath(int entryId) const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QString("%1/posters/%2.jpg").arg(dataDir).arg(entryId);
}

void LibraryManager::downloadPoster(int entryId, const QString &remoteUrl)
{
    const QString outPath = posterOutputPath(entryId);

    if (QFileInfo::exists(outPath)) {
        const int row = rowForId(entryId);
        if (row < 0) return;
        const QString localUrl = QUrl::fromLocalFile(outPath).toString();
        if (m_entries[row].posterUrl != localUrl) {
            m_entries[row].posterUrl = localUrl;
            updateEntryPosterInDatabase(entryId, localUrl);
            emit dataChanged(index(row), index(row));
        }
        return;
    }

    if (!remoteUrl.startsWith("http")) {
        generatePosterFromVideo(entryId);
        return;
    }

    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(remoteUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, entryId, outPath, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            generatePosterFromVideo(entryId);
            return;
        }

        const QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            generatePosterFromVideo(entryId);
            return;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly)) {
            generatePosterFromVideo(entryId);
            return;
        }
        f.write(data);
        f.close();

        const int row = rowForId(entryId);
        if (row < 0) return;
        const QString localUrl = QUrl::fromLocalFile(outPath).toString();
        m_entries[row].posterUrl = localUrl;
        updateEntryPosterInDatabase(entryId, localUrl);
        emit dataChanged(index(row), index(row));
    });
}

QString LibraryManager::ffmpegPath() const
{
    const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (QFileInfo::exists(bundled)) return bundled;
#ifdef QT_DEBUG
    if (QFileInfo::exists(kDevFfmpegPath)) return kDevFfmpegPath;
#endif
    return QStringLiteral("ffmpeg");
}

static constexpr int kScrubIntervalSec = 10; // Preserved exactly as requested

QString LibraryManager::scrubCacheDir(int entryId, int season, int episode) const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scrub_thumbs/";
    return season > 0
               ? base + QString("show_%1_s%2e%3").arg(entryId).arg(season).arg(episode)
               : base + QString("movie_%1").arg(entryId);
}

void LibraryManager::ensureScrubThumbnails(int entryId, int season, int episode,
                                           const QString &filePath, qint64 durationMs)
{
    if (filePath.isEmpty() || durationMs <= 0) return;

    const QString dir        = scrubCacheDir(entryId, season, episode);
    const QString doneMarker = dir + "/.complete";
    QDir d(dir);

    if (QFileInfo::exists(doneMarker)) {
        const int existing = d.entryList(QStringList() << "thumb_*.jpg", QDir::Files).count();
        if (existing > 0) {
            emit scrubThumbnailsReady(entryId, season, episode, existing, kScrubIntervalSec);
            return;
        }
    }
    d.mkpath(".");

    // Terminate any previous orphan FFmpeg scrubbing process to save RAM
    if (m_scrubProcess) {
        if (m_scrubProcess->state() != QProcess::NotRunning) {
            m_scrubProcess->kill();
            m_scrubProcess->waitForFinished(500);
        }
        m_scrubProcess->deleteLater();
        m_scrubProcess = nullptr;
    }

    const QString exe = ffmpegPath();
    QProcess *proc = new QProcess(this);
    m_scrubProcess = proc;

    QStringList args;
    args << "-y"
         << "-threads" << "1"                   // Single thread keeps RAM ultra low during generation
         << "-hwaccel" << "auto"
         << "-skip_frame" << "nokey"
         << "-i" << filePath
         << "-vf" << QString("fps=1/%1,scale=135:-1").arg(kScrubIntervalSec) // Optimized 135px scale
         << "-qscale:v" << "11"                 // Optimized quality (70% smaller file & texture RAM size)
         << dir + "/thumb_%04d.jpg";

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, dir, doneMarker, entryId, season, episode](int exitCode, QProcess::ExitStatus status) {
                const int count = QDir(dir).entryList(QStringList() << "thumb_*.jpg", QDir::Files).count();
                if (count > 0) {
                    QFile marker(doneMarker);
                    if (marker.open(QIODevice::WriteOnly)) marker.close();
                    emit scrubThumbnailsReady(entryId, season, episode, count, kScrubIntervalSec);
                }
                if (m_scrubProcess == proc) m_scrubProcess = nullptr;
                proc->deleteLater();
            });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        if (m_scrubProcess == proc) m_scrubProcess = nullptr;
        proc->deleteLater();
    });

    proc->start(exe, args);
}

QString LibraryManager::episodeSubtitle(int entryId, int season, int episode) const
{
    const int row = rowForId(entryId);
    if (row < 0) return QString();
    const LibraryEntry &entry = m_entries.at(row);
    ensureEpisodesLoaded(entry);

    for (const EpisodeEntry &ep : entry.episodes) {
        if (ep.season == season && ep.episode == episode)
            return ep.subtitlePath;
    }
    return QString();
}

void LibraryManager::setEpisodeSubtitle(int entryId, int season, int episode, const QString &subtitlePath)
{
    const int row = rowForId(entryId);
    if (row < 0) return;

    LibraryEntry &e = m_entries[row];
    ensureEpisodesLoaded(e);

    for (EpisodeEntry &ep : e.episodes) {
        if (ep.season == season && ep.episode == episode) {
            ep.subtitlePath = subtitlePath;
            break;
        }
    }
    updateEpisodeSubtitleInDatabase(entryId, season, episode, subtitlePath);
}

QString LibraryManager::movieSubtitle(int entryId) const
{
    const int row = rowForId(entryId);
    if (row < 0) return QString();
    return m_entries.at(row).subtitlePath;
}

void LibraryManager::setMovieSubtitle(int entryId, const QString &subtitlePath)
{
    const int row = rowForId(entryId);
    if (row < 0) return;
    m_entries[row].subtitlePath = subtitlePath;
    updateEntrySubtitleInDatabase(entryId, subtitlePath);
}

void LibraryManager::generatePosterFromVideo(int entryId)
{
    const int row = rowForId(entryId);
    if (row < 0) return;

    const LibraryEntry &e = m_entries.at(row);
    if (!e.posterUrl.isEmpty()) return;

    const QString outPath = posterOutputPath(entryId);
    if (QFileInfo::exists(outPath)) {
        const QString localUrl = QUrl::fromLocalFile(outPath).toString();
        m_entries[row].posterUrl = localUrl;
        updateEntryPosterInDatabase(entryId, localUrl);
        emit dataChanged(index(row), index(row));
        return;
    }

    QString videoPath;
    if (e.kind == "Movie") {
        videoPath = e.filePath;
    } else {
        ensureEpisodesLoaded(e);
        if (!e.episodes.isEmpty())
            videoPath = e.episodes.first().filePath;
    }

    if (videoPath.isEmpty() || !QFileInfo::exists(videoPath)) return;
    QDir().mkpath(QFileInfo(outPath).absolutePath());

    QProcess *proc = new QProcess(this);
    const QStringList args = {
        "-y", "-ss", "60", "-i", videoPath,
        "-frames:v", "1", "-vf", "scale=300:-1", "-q:v", "3", outPath
    };

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, entryId, outPath, proc](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (status == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(outPath)) {
                    const int row = rowForId(entryId);
                    if (row >= 0) {
                        const QString localUrl = QUrl::fromLocalFile(outPath).toString();
                        m_entries[row].posterUrl = localUrl;
                        updateEntryPosterInDatabase(entryId, localUrl);
                        emit dataChanged(index(row), index(row));
                    }
                }
            });

    connect(proc, &QProcess::errorOccurred, this, [proc](QProcess::ProcessError) { proc->deleteLater(); });
    proc->start(ffmpegPath(), args);
}

void LibraryManager::removeAt(int row)
{
    if (row < 0 || row >= m_entries.count()) return;
    const int     id    = m_entries.at(row).id;
    const QString title = m_entries.at(row).title;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    deleteEntryFromDatabase(id);
    emit itemRemoved(title);
}

QString LibraryManager::guessTitleFromFileName(const QString &fileName) const
{
    QString title = fileName;
    title.replace(QRegularExpression("[._]"), " ");
    title.remove(QRegularExpression(R"(\b(19|20)\d{2}\b.*$)"));
    title.remove(QRegularExpression(R"(\s*[Ss]\d{1,2}.*$)"));
    return title.trimmed();
}

int LibraryManager::rowForId(int entryId) const
{
    for (int i = 0; i < m_entries.count(); ++i)
        if (m_entries.at(i).id == entryId) return i;
    return -1;
}

void LibraryManager::openDatabase()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir + "/thumbnails");
    QDir().mkpath(dataDir + "/posters");
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dataDir + "/library.db");
    m_db.open();


    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode = WAL;");
    pragma.exec("PRAGMA cache_size = -2000;"); // 2MB
}

void LibraryManager::createTablesIfNeeded()
{
    QSqlQuery q(m_db);
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS library_items (
            id            INTEGER PRIMARY KEY,
            title         TEXT,
            rating        REAL,
            kind          TEXT,
            progress      REAL,
            file_path     TEXT,
            poster_url    TEXT,
            duration      TEXT,
            imdb_id       TEXT,
            sort_order    INTEGER DEFAULT 0,
            position_ms   INTEGER DEFAULT 0,
            duration_ms   INTEGER DEFAULT 0,
            subtitle_path TEXT
        )
    )");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS episodes (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            show_id        INTEGER,
            season         INTEGER,
            episode        INTEGER,
            title          TEXT,
            file_path      TEXT,
            imdb_rating    TEXT,
            duration       TEXT,
            position_ms    INTEGER DEFAULT 0,
            duration_ms    INTEGER DEFAULT 0,
            thumbnail_path TEXT,
            subtitle_path  TEXT
        )
    )");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key   TEXT PRIMARY KEY,
            value TEXT
        )
    )");


    q.exec("CREATE INDEX IF NOT EXISTS idx_episodes_show_id ON episodes(show_id)");
}

void LibraryManager::migrateAddThumbnailColumnIfNeeded()
{
    QSqlQuery check(m_db);
    check.exec("PRAGMA table_info(episodes)");
    bool has = false;
    while (check.next())
        if (check.value(1).toString() == "thumbnail_path") { has = true; break; }
    if (!has) QSqlQuery(m_db).exec("ALTER TABLE episodes ADD COLUMN thumbnail_path TEXT");
}

void LibraryManager::migrateAddSortOrderColumnIfNeeded()
{
    QSqlQuery check(m_db);
    check.exec("PRAGMA table_info(library_items)");
    bool has = false;
    while (check.next())
        if (check.value(1).toString() == "sort_order") { has = true; break; }
    if (!has) {
        QSqlQuery(m_db).exec("ALTER TABLE library_items ADD COLUMN sort_order INTEGER DEFAULT 0");
        QSqlQuery ids(m_db);
        ids.exec("SELECT id FROM library_items ORDER BY id");
        QSqlQuery upd(m_db);
        upd.prepare("UPDATE library_items SET sort_order=:so WHERE id=:id");
        int pos = 0;
        while (ids.next()) {
            upd.bindValue(":so", pos++);
            upd.bindValue(":id", ids.value(0).toInt());
            upd.exec();
        }
    }
}

void LibraryManager::migrateAddMovieProgressColumnsIfNeeded()
{
    QSqlQuery check(m_db);
    check.exec("PRAGMA table_info(library_items)");
    bool hasPos = false, hasDur = false;
    while (check.next()) {
        const QString col = check.value(1).toString();
        if (col == "position_ms") hasPos = true;
        if (col == "duration_ms") hasDur = true;
    }
    if (!hasPos) QSqlQuery(m_db).exec("ALTER TABLE library_items ADD COLUMN position_ms INTEGER DEFAULT 0");
    if (!hasDur) QSqlQuery(m_db).exec("ALTER TABLE library_items ADD COLUMN duration_ms INTEGER DEFAULT 0");
}

void LibraryManager::migrateAddSubtitleColumnsIfNeeded()
{
    QSqlQuery checkEp(m_db);
    checkEp.exec("PRAGMA table_info(episodes)");
    bool epHas = false;
    while (checkEp.next())
        if (checkEp.value(1).toString() == "subtitle_path") { epHas = true; break; }
    if (!epHas) QSqlQuery(m_db).exec("ALTER TABLE episodes ADD COLUMN subtitle_path TEXT");

    QSqlQuery checkLib(m_db);
    checkLib.exec("PRAGMA table_info(library_items)");
    bool libHas = false;
    while (checkLib.next())
        if (checkLib.value(1).toString() == "subtitle_path") { libHas = true; break; }
    if (!libHas) QSqlQuery(m_db).exec("ALTER TABLE library_items ADD COLUMN subtitle_path TEXT");
}

void LibraryManager::migrateLocalizeRemotePostersIfNeeded()
{
    for (const LibraryEntry &e : std::as_const(m_entries)) {
        if (e.posterUrl.startsWith("http"))
            downloadPoster(e.id, e.posterUrl);
    }
}

void LibraryManager::loadFromDatabase()
{
    QSqlQuery q(m_db);
    q.exec("SELECT id,title,rating,kind,progress,file_path,poster_url,"
           "duration,imdb_id,sort_order,position_ms,duration_ms,subtitle_path "
           "FROM library_items ORDER BY sort_order,id");
    while (q.next()) {
        LibraryEntry e;
        e.id           = q.value(0).toInt();
        e.title        = q.value(1).toString();
        e.rating       = q.value(2).toDouble();
        e.kind         = q.value(3).toString();
        e.progress     = q.value(4).toDouble();
        e.filePath     = q.value(5).toString();
        e.posterUrl    = q.value(6).toString();
        e.duration     = q.value(7).toString();
        e.imdbId       = q.value(8).toString();
        e.sortOrder    = q.value(9).toInt();
        e.positionMs   = q.value(10).toLongLong();
        e.durationMs   = q.value(11).toLongLong();
        e.subtitlePath = q.value(12).toString();
        if (e.id >= m_nextId) m_nextId = e.id + 1;
        m_entries.append(e);
    }
    // Note: Episodes are loaded on-demand per show
}

void LibraryManager::saveEntryToDatabase(const LibraryEntry &e)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO library_items "
              "(id,title,rating,kind,progress,file_path,poster_url,duration,"
              "imdb_id,sort_order,position_ms,duration_ms) "
              "VALUES(:id,:t,:r,:k,:p,:fp,:pu,:d,:iid,:so,:pms,:dms)");
    q.bindValue(":id",  e.id); q.bindValue(":t",   e.title); q.bindValue(":r",   e.rating);
    q.bindValue(":k",   e.kind); q.bindValue(":p",   e.progress); q.bindValue(":fp",  e.filePath);
    q.bindValue(":pu",  e.posterUrl); q.bindValue(":d",   e.duration); q.bindValue(":iid", e.imdbId);
    q.bindValue(":so",  e.sortOrder); q.bindValue(":pms", e.positionMs); q.bindValue(":dms", e.durationMs);
    q.exec();
}

void LibraryManager::updateEntryMetadataInDatabase(const LibraryEntry &e)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET title=:t,rating=:r,poster_url=:pu,"
              "duration=:d,imdb_id=:iid WHERE id=:id");
    q.bindValue(":t",   e.title); q.bindValue(":r",   e.rating); q.bindValue(":pu",  e.posterUrl);
    q.bindValue(":d",   e.duration); q.bindValue(":iid", e.imdbId); q.bindValue(":id",  e.id);
    q.exec();
}

void LibraryManager::saveEpisodeToDatabase(int showId, const EpisodeEntry &ep)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO episodes "
              "(show_id,season,episode,title,file_path,imdb_rating,duration,"
              "position_ms,duration_ms,thumbnail_path) "
              "VALUES(:sid,:s,:e,:t,:fp,:ir,:d,:pms,:dms,:tp)");
    q.bindValue(":sid", showId); q.bindValue(":s",   ep.season); q.bindValue(":e",   ep.episode);
    q.bindValue(":t",   ep.title); q.bindValue(":fp",  ep.filePath); q.bindValue(":ir",  ep.imdbRating);
    q.bindValue(":d",   ep.duration); q.bindValue(":pms", ep.positionMs); q.bindValue(":dms", ep.durationMs);
    q.bindValue(":tp",  ep.thumbnailPath);
    q.exec();
}

void LibraryManager::updateEpisodeMetadataInDatabase(int showId, const EpisodeEntry &ep)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE episodes SET title=:t,imdb_rating=:ir WHERE show_id=:sid AND season=:s AND episode=:e");
    q.bindValue(":t", ep.title); q.bindValue(":ir", ep.imdbRating); q.bindValue(":sid", showId);
    q.bindValue(":s", ep.season); q.bindValue(":e", ep.episode);
    q.exec();
}

void LibraryManager::updateEpisodeThumbnailInDatabase(int showId, int season, int episode, const QString &path)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE episodes SET thumbnail_path=:tp WHERE show_id=:sid AND season=:s AND episode=:e");
    q.bindValue(":tp", path); q.bindValue(":sid", showId); q.bindValue(":s", season); q.bindValue(":e", episode);
    q.exec();
}

void LibraryManager::updateEpisodeSubtitleInDatabase(int showId, int season, int episode, const QString &subtitlePath)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE episodes SET subtitle_path=:sp WHERE show_id=:sid AND season=:s AND episode=:e");
    q.bindValue(":sp", subtitlePath); q.bindValue(":sid", showId); q.bindValue(":s", season); q.bindValue(":e", episode);
    q.exec();
}

void LibraryManager::updateEntrySubtitleInDatabase(int entryId, const QString &subtitlePath)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET subtitle_path=:sp WHERE id=:id");
    q.bindValue(":sp", subtitlePath); q.bindValue(":id", entryId);
    q.exec();
}

void LibraryManager::updateEpisodePositionInDatabase(int showId, int season, int episode, qint64 positionMs, qint64 durationMs)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE episodes SET position_ms=:pms,duration_ms=:dms WHERE show_id=:sid AND season=:s AND episode=:e");
    q.bindValue(":pms", positionMs); q.bindValue(":dms", durationMs); q.bindValue(":sid", showId);
    q.bindValue(":s", season); q.bindValue(":e", episode);
    q.exec();
}

void LibraryManager::updateEntryPositionInDatabase(int entryId, qint64 positionMs, qint64 durationMs)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET position_ms=:pms,duration_ms=:dms WHERE id=:id");
    q.bindValue(":pms", positionMs); q.bindValue(":dms", durationMs); q.bindValue(":id", entryId);
    q.exec();
}

void LibraryManager::deleteEntryFromDatabase(int entryId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM library_items WHERE id=:id");
    q.bindValue(":id", entryId); q.exec();
    q.prepare("DELETE FROM episodes WHERE show_id=:id");
    q.bindValue(":id", entryId); q.exec();
}

void LibraryManager::persistSortOrder()
{
    for (int i = 0; i < m_entries.count(); ++i) {
        m_entries[i].sortOrder = i;
        updateSortOrderInDatabase(m_entries.at(i).id, i);
    }
}

void LibraryManager::updateSortOrderInDatabase(int entryId, int sortOrder)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET sort_order=:so WHERE id=:id");
    q.bindValue(":so", sortOrder); q.bindValue(":id", entryId);
    q.exec();
}

void LibraryManager::updateEntryProgressInDatabase(int entryId, double progress)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET progress=:p WHERE id=:id");
    q.bindValue(":p", progress); q.bindValue(":id", entryId);
    q.exec();
}

void LibraryManager::updateEntryPosterInDatabase(int entryId, const QString &posterPath)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE library_items SET poster_url=:pu WHERE id=:id");
    q.bindValue(":pu", posterPath); q.bindValue(":id", entryId);
    q.exec();
}

void LibraryManager::saveSetting(const QString &key, const QString &value)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO settings (key, value) VALUES (:k, :v)");
    q.bindValue(":k", key); q.bindValue(":v", value);
    q.exec();
}

QString LibraryManager::getSetting(const QString &key, const QString &defaultValue) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM settings WHERE key = :k");
    q.bindValue(":k", key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}