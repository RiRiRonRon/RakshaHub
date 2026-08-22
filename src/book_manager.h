#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QUrl>
#include <QProcess>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QFontDatabase>
#include <QDir>
#include <QSettings>

class BookManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int rowCount READ count NOTIFY booksChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        AuthorRole,
        FormatRole,
        FilePathRole,
        CoverPathRole,
        ProgressRole,
        CurrentPageRole,
        TotalPagesRole,
        BookIdRole,
        FileSizeRole,
    };

    explicit BookManager(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.count(); }

    Q_INVOKABLE void addBook(const QUrl &fileUrl);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void updateProgress(int bookId, int currentPage, int totalPages);

    Q_INVOKABLE QString readerUrl(int bookId) const;
    Q_INVOKABLE QString pdfjsViewerPath() const;

    Q_INVOKABLE void ensureCovers();

    // Font management
    Q_INVOKABLE QStringList availableFonts() const;

    // Settings
    Q_INVOKABLE void saveReaderSettings(
        int bookId,
        const QString &theme,
        int fontSize,
        const QString &flow,
        int padding,
        const QString &textColor,
        int brightness,
        const QString &fontFamily,
        int lineSpacing
        );

    Q_INVOKABLE QVariantMap readerSettings(int bookId) const;

    Q_INVOKABLE QString lastLocation(int bookId) const;
    Q_INVOKABLE void    saveLocation(int bookId, const QString &cfi);

    Q_INVOKABLE void savePdfBinaryFile(int bookId, const QString &base64Data);

    // Heilgihts
    Q_INVOKABLE int addHighlight(
        int bookId,
        const QString &chapter,
        const QString &locator,
        const QString &text,
        const QString &color
        );

    Q_INVOKABLE void removeHighlight(int highlightId);
    Q_INVOKABLE void removeHighlightByLocator(const QString &locator);
    Q_INVOKABLE void updateHighlightColor(const QString &locator, const QString &color);

    Q_INVOKABLE QVariantList getHighlightsForBook(int bookId) const;
    Q_INVOKABLE QVariantList getAllHighlights() const;

signals:
    void bookAdded(const QString &title);
    void bookRemoved(const QString &title);
    void duplicateBook(const QString &title);
    void booksChanged();
    void highlightsChanged();
    void pdfSaved(int bookId);

private:
    struct BookEntry {
        int     id           = 0;
        QString title;
        QString author;
        QString format;
        QString filePath;
        QString coverPath;
        double  progress     = 0.0;
        int     currentPage  = 0;
        int     totalPages   = 0;
        int     sortOrder    = 0;
        qint64  fileSize     = 0;
    };

    QList<BookEntry>  m_entries;
    int               m_nextId = 1;
    QSqlDatabase      m_db;

    mutable QStringList m_cachedFonts;

    QList<BookEntry>  m_coverQueue;
    bool              m_isExtractingCover = false;
    int               m_currentCoverId    = -1;

    void scheduleNextCover();
    void processNextCover();
    void applyCoverPath(int bookId, const QString &coverPath);

    static bool extractZipArchive(const QString &zipPath, const QString &destDir);
    void ensurePdfJsHighlightHelper(const QString &viewerHtmlPath) const;

    QString appDataDir() const;
    QString coverOutputPath(int bookId) const;
    QString ghostscriptPath() const;

    void extractCover(const BookEntry &entry);
    void extractPdfCover(const BookEntry &entry);
    void extractEpubCover(const BookEntry &entry);
    void extractCbzCover(const BookEntry &entry);

    QString guessTitleFromFileName(const QString &fileName) const;
    int     rowForId(int bookId) const;

    void openDatabase();
    void createTablesIfNeeded();
    void loadFromDatabase();
    void saveToDatabase(const BookEntry &entry);

    void updateProgressInDatabase(int bookId, int currentPage, int totalPages, double progress);
    void updateCoverInDatabase(int bookId, const QString &coverPath);
    void deleteFromDatabase(int bookId);
};