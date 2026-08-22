#include "book_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QProcess>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFontDatabase>
#include <QSettings>
#include <QTimer>

#include <algorithm>

static bool isValidCoverFile(const QString &path)
{
    QFileInfo fi(path);
    return fi.exists() && fi.size() > 0;
}

static QString formatToLocalTime(const QString &dbDateTimeStr)
{
    if (dbDateTimeStr.isEmpty())
        return QString();

    QDateTime dt = QDateTime::fromString(dbDateTimeStr, "yyyy-MM-dd HH:mm:ss");
    if (!dt.isValid()) {
        dt = QDateTime::fromString(dbDateTimeStr, Qt::ISODate);
    }

    if (dt.isValid()) {
        dt.setTimeSpec(Qt::UTC);
        return dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
    }

    return dbDateTimeStr;
}

// Fast archive extraction
bool BookManager::extractZipArchive(const QString &zipPath, const QString &destDir)
{
    QDir().mkpath(destDir);
    const QString natZip = QDir::toNativeSeparators(zipPath);
    const QString natDest = QDir::toNativeSeparators(destDir);

    QProcess proc;
    proc.start("tar", {"-xf", natZip, "-C", natDest});
    if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
        return true;
    }

    QProcess psProc;
    psProc.start("powershell", {
                                   "-NoProfile", "-NonInteractive", "-Command",
                                   QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%1', '%2')")
                                       .arg(natZip, natDest)
                               });
    return psProc.waitForFinished(8000) && psProc.exitCode() == 0;
}

void BookManager::ensurePdfJsHighlightHelper(const QString &viewerHtmlPath) const
{
    QFile f(viewerHtmlPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(f.readAll());
    f.close();

    // Unblock strict Content Security Policy in viewer.html
    content.replace(QRegularExpression(R"(script-src\s+[^;"'>]+)", QRegularExpression::CaseInsensitiveOption),
                    "script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'");
    content.replace(QRegularExpression(R"(default-src\s+[^;"'>]+)", QRegularExpression::CaseInsensitiveOption),
                    "default-src 'self' 'unsafe-inline' blob: data:");

    const QString helperJsContent = QStringLiteral(R"RAW_JS(
(function() {
    var _autoSaveTimer = null;
    var _currentPendingSelectionText = "";
    var _lastRecordedText = "";
    var _lastRecordedTime = 0;

    // Track text selection continuously as user selects text in PDF.js
    document.addEventListener("selectionchange", function() {
        var sel = window.getSelection();
        if (sel && !sel.isCollapsed) {
            var str = (sel.toString() || "").trim();
            if (str.length >= 2) {
                _currentPendingSelectionText = str;
            }
        }
    }, { passive: true });

    function recordPdfHighlight() {
        var text = _currentPendingSelectionText;

        if (!text) {
            var sel = window.getSelection();
            if (sel) text = (sel.toString() || "").trim();
        }

        if (!text || text.length < 2) return;

        var now = Date.now();
        if (text === _lastRecordedText && (now - _lastRecordedTime) < 2000) return;

        var pdfApp = window.PDFViewerApplication;
        var pageNum = (pdfApp && pdfApp.page) ? pdfApp.page : 1;
        var locId = "pdfpage:" + pageNum + "_" + now;

        _lastRecordedText = text;
        _lastRecordedTime = now;
        _currentPendingSelectionText = "";

        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({
                type: "addHighlight",
                text: text,
                color: "#ffe082",
                locator: locId,
                chapter: "Page " + pageNum
            }));
        }
    }

    // Heavy PDF binary export (ONLY runs when closing or explicitly requested)
    window.savePdfToHost = function() {
        clearTimeout(_autoSaveTimer);
        var pdfApp = window.PDFViewerApplication;
        if (!pdfApp || !pdfApp.pdfDocument) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ type: "pdfSaveFailed" }));
            }
            return;
        }

        try {
            var commitPromise = Promise.resolve();
            if (pdfApp.pdfViewer && typeof pdfApp.pdfViewer.commitOrChange === "function") {
                try { commitPromise = Promise.resolve(pdfApp.pdfViewer.commitOrChange()); } catch(e) {}
            }

            commitPromise.then(function() {
                var storage = pdfApp.pdfDocument.annotationStorage || (pdfApp.pdfViewer ? pdfApp.pdfViewer.annotationStorage : null);
                return pdfApp.pdfDocument.saveDocument(storage);
            }).then(function(data) {
                if (!data || !data.byteLength) {
                    if (window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage(JSON.stringify({ type: "pdfSaveFailed" }));
                    }
                    return;
                }

                var blob = new Blob([data], { type: "application/pdf" });
                var reader = new FileReader();
                reader.onloadend = function() {
                    if (!reader.result) {
                        if (window.chrome && window.chrome.webview) {
                            window.chrome.webview.postMessage(JSON.stringify({ type: "pdfSaveFailed" }));
                        }
                        return;
                    }
                    var base64 = reader.result.split(",")[1];
                    if (base64 && window.chrome && window.chrome.webview) {
                        window.chrome.webview.postMessage(JSON.stringify({
                            type: "savePdfBinary",
                            data: base64
                        }));
                    }
                };
                reader.readAsDataURL(blob);
            }).catch(function(err) {
                console.error("PDF.js saveDocument error:", err);
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: "pdfSaveFailed" }));
                }
            });
        } catch(e) {
            console.error("savePdfToHost error:", e);
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ type: "pdfSaveFailed" }));
            }
        }
    };

    window.goToPdfPage = function(pageNumber) {
        var pdfApp = window.PDFViewerApplication;
        if (pdfApp && typeof pageNumber === "number" && pageNumber > 0) {
            pdfApp.page = pageNumber;
        }
    };

    function triggerAutoSave() {
        recordPdfHighlight();
        clearTimeout(_autoSaveTimer);
        _autoSaveTimer = setTimeout(window.savePdfToHost, 300);
    }

    function setupPdfEvents() {
        var checkInterval = setInterval(function() {
            var pdfApp = window.PDFViewerApplication;
            if (pdfApp && pdfApp.eventBus && pdfApp.pdfDocument) {
                clearInterval(checkInterval);

                pdfApp.eventBus.on("pagechanging", function(e) { sendPdfProgress(e ? e.pageNumber : null); });
                pdfApp.eventBus.on("pagechange", function(e) { sendPdfProgress(e ? e.pageNumber : null); });

                var saveEvents = [
                    "annotationeditorstateschanged",
                    "annotationeditorparamschanged",
                    "annotationstoragechanged",
                    "annotationeditoradded",
                    "annotationeditorremoved",
                    "editingaction",
                    "download"
                ];

                saveEvents.forEach(function(evt) {
                    try { pdfApp.eventBus.on(evt, triggerAutoSave); } catch(e) {}
                });

                if (pdfApp.pdfDocument.annotationStorage) {
                    var storage = pdfApp.pdfDocument.annotationStorage;
                    var origSet = storage.setValue || storage.set;
                    if (typeof origSet === "function") {
                        storage.setValue = function() {
                            var r = origSet.apply(this, arguments);
                            triggerAutoSave();
                            return r;
                        };
                    }
                    if (typeof storage.onSetModified === "function" || storage.onSetModified === null) {
                        var oldMod = storage.onSetModified;
                        storage.onSetModified = function() {
                            if (typeof oldMod === "function") oldMod.apply(this, arguments);
                            triggerAutoSave();
                        };
                    }
                }

                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: "pdfLoaded" }));
                }
            }
        }, 100);

        var vCont = document.getElementById("viewerContainer");
        if (vCont) {
            vCont.addEventListener("scroll", function() { sendPdfProgress(); }, { passive: true });
            vCont.addEventListener("mouseup", function() { setTimeout(triggerAutoSave, 200); });
            vCont.addEventListener("keyup", function() { setTimeout(triggerAutoSave, 200); });
            vCont.addEventListener("touchend", function() { setTimeout(triggerAutoSave, 200); });
        }

        window.addEventListener("beforeunload", function() { if (window.savePdfToHost) window.savePdfToHost(); });
        window.addEventListener("pagehide", function() { if (window.savePdfToHost) window.savePdfToHost(); });
    }

    var lastPage = -1;
    function sendPdfProgress(overridePage) {
        var pdfApp = window.PDFViewerApplication;
        if (!pdfApp || !pdfApp.pdfDocument) return;
        var page = (typeof overridePage === "number" && overridePage > 0) ? overridePage : (pdfApp.page || 1);
        var total = pdfApp.pagesCount || 1;
        if (page === lastPage && total > 1 && typeof overridePage === "undefined") return;
        lastPage = page;

        var pct = total > 1 ? (page - 1) / (total - 1) : 0;
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({
                type: "progress",
                page: page,
                totalPages: total,
                pct: pct,
                cfi: "pdfpage:" + page
            }));
        }
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", setupPdfEvents);
    } else {
        setupPdfEvents();
    }
})();
)RAW_JS");


    QFileInfo fi(viewerHtmlPath);
    const QString helperJsPath = fi.absoluteDir().filePath("pdf_hl_helper.js");

    QFile helperJsFile(helperJsPath);
    if (helperJsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        helperJsFile.write(helperJsContent.toUtf8());
        helperJsFile.close();
    }

    // Linking external script file in viewer.html
    static const QRegularExpression oldScriptRe(R"(<script\s+id="pdf-hl-helper-script"[^>]*>[\s\S]*?</script>)", QRegularExpression::CaseInsensitiveOption);
    content.remove(oldScriptRe);

    const QString scriptTag = QStringLiteral("<script id=\"pdf-hl-helper-script\" src=\"pdf_hl_helper.js\"></script>");

    if (content.contains("</body>")) {
        content.replace("</body>", scriptTag + "\n</body>");
    } else {
        content += "\n" + scriptTag;
    }

    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(content.toUtf8());
        f.close();
    }
}

BookManager::BookManager(QObject *parent)
    : QAbstractListModel(parent)
{
    const QString fontsDir = QCoreApplication::applicationDirPath() + "/fonts";
    QDir dir(fontsDir);

    if (dir.exists()) {
        const QStringList fontFiles = dir.entryList({"*.ttf", "*.otf", "*.TTF", "*.OTF"}, QDir::Files);

        for (const QString &fontFile : std::as_const(fontFiles)) {
            const QString fontPath = dir.filePath(fontFile);
            const int fontId = QFontDatabase::addApplicationFont(fontPath);

            if (fontId == -1)
                qWarning() << "Failed to load font:" << fontPath;
        }
    }

    QDir(appDataDir() + "/epub_tmp").removeRecursively();
    QDir(appDataDir() + "/cbz_tmp").removeRecursively();
    QDir(appDataDir() + "/pdf_temp").removeRecursively();

    openDatabase();
    createTablesIfNeeded();
    loadFromDatabase();

    ensurePdfJsHighlightHelper(pdfjsViewerPath());
}

// Model

int BookManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_entries.count();
}

QVariant BookManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.count())
        return {};

    const BookEntry &e = m_entries.at(index.row());

    switch (role) {
    case TitleRole:
        return e.title;

    case AuthorRole:
        return e.author;

    case FormatRole:
        return e.format;

    case FilePathRole:
        return e.filePath;

    case CoverPathRole: {
        if (e.coverPath.isEmpty())
            return "";

        QUrl u = QUrl::fromLocalFile(e.coverPath);

        u.setQuery(QStringLiteral("v=%1")
                       .arg(QFileInfo(e.coverPath).lastModified().toSecsSinceEpoch()));

        return u.toString();
    }

    case ProgressRole:
        return e.progress;

    case CurrentPageRole:
        return e.currentPage;

    case TotalPagesRole:
        return e.totalPages;

    case BookIdRole:
        return e.id;

    case FileSizeRole:
        return e.fileSize;

    default:
        return {};
    }
}

QHash<int, QByteArray> BookManager::roleNames() const
{
    return {
             { TitleRole,       "title"       },
             { AuthorRole,      "author"      },
             { FormatRole,      "format"      },
             { FilePathRole,    "filePath"    },
             { CoverPathRole,   "coverPath"   },
             { ProgressRole,    "progress"    },
             { CurrentPageRole, "currentPage" },
             { TotalPagesRole,  "totalPages"  },
             { BookIdRole,      "bookId"      },
             { FileSizeRole,    "fileSize"    },
             };
}

// Add / Remove

void BookManager::addBook(const QUrl &fileUrl)
{
    const QString filePath = fileUrl.toLocalFile();

    for (const BookEntry &e : std::as_const(m_entries)) {
        if (e.filePath.compare(filePath, Qt::CaseInsensitive) == 0) {
            emit duplicateBook(e.title);
            return;
        }
    }

    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();

    QString format;
    if (ext == "pdf")
        format = "PDF";
    else if (ext == "epub")
        format = "EPUB";
    else if (ext == "cbz")
        format = "CBZ";
    else
        return;

    const int newRow = m_entries.count();

    BookEntry entry;
    entry.id        = m_nextId++;
    entry.title     = guessTitleFromFileName(fi.completeBaseName());
    entry.format    = format;
    entry.filePath  = filePath;
    entry.fileSize  = fi.size();
    entry.sortOrder = newRow;

    beginInsertRows(QModelIndex(), newRow, newRow);
    m_entries.append(entry);
    endInsertRows();

    saveToDatabase(entry);

    emit bookAdded(entry.title);
    emit booksChanged();

    extractCover(entry);
}

void BookManager::removeAt(int row)
{
    if (row < 0 || row >= m_entries.count())
        return;

    const int     id     = m_entries.at(row).id;
    const QString title  = m_entries.at(row).title;
    const QString cover  = m_entries.at(row).coverPath;

    if (!cover.isEmpty())
        QFile::remove(cover);

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();

    deleteFromDatabase(id);

    emit bookRemoved(title);
    emit booksChanged();
}

// Progress

void BookManager::updateProgress(int bookId, int currentPage, int totalPages)
{
    const int row = rowForId(bookId);
    if (row < 0)
        return;

    BookEntry &e = m_entries[row];

    e.currentPage = currentPage;
    e.totalPages  = totalPages;
    e.progress    = (totalPages > 0)
                     ? qBound(0.0, static_cast<double>(currentPage) / totalPages, 1.0)
                     : 0.0;

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);

    updateProgressInDatabase(bookId, currentPage, totalPages, e.progress);
}

// Reader URL

QString BookManager::pdfjsViewerPath() const
{
    return QCoreApplication::applicationDirPath() + "/pdfjs/web/viewer.html";
}

QString BookManager::readerUrl(int bookId) const
{
    const int row = rowForId(bookId);
    if (row < 0)
        return {};

    const BookEntry &e = m_entries.at(row);

    if (e.format == "PDF") {
        const QString viewerPath = pdfjsViewerPath();
        ensurePdfJsHighlightHelper(viewerPath);

        const QString tempDir = appDataDir() + "/pdf_temp";
        QDir().mkpath(tempDir);
        const QString tempPath = tempDir + "/" + QString::number(bookId) + "_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".pdf";

        if (!QFile::copy(e.filePath, tempPath)) {
            qWarning() << "BookManager: failed to copy PDF to temporary path:" << tempPath;
        }

        const QString fileUrl = QUrl::fromLocalFile(tempPath).toString();

        QString pageHash;
        const QString loc = lastLocation(bookId);
        if (loc.startsWith("pdfpage:")) {
            bool ok = false;
            int pNum = loc.mid(8).toInt(&ok);
            if (ok && pNum > 0) {
                pageHash = QString("#page=%1").arg(pNum);
            }
        }
        if (pageHash.isEmpty()) {
            pageHash = "#v=" + QString::number(QDateTime::currentMSecsSinceEpoch());
        }

        return QUrl::fromLocalFile(viewerPath).toString()
               + "?file=" + QUrl::toPercentEncoding(fileUrl)
               + "&v=" + QString::number(QDateTime::currentMSecsSinceEpoch())
               + pageHash;
    }

    if (e.format == "EPUB") {
        const QString extractDir = appDataDir() + "/readers/" + QString::number(bookId) + "/";
        QDir().mkpath(extractDir);

        if (!QFileInfo::exists(extractDir + "META-INF/container.xml")) {
            extractZipArchive(e.filePath, extractDir);
        }

        QString opfPath;
        {
            QFile cf(extractDir + "META-INF/container.xml");
            if (cf.open(QIODevice::ReadOnly)) {
                const QString xml = QString::fromUtf8(cf.readAll());
                const QRegularExpression re("full-path=['\"]([^'\"]+)['\"]");
                const auto m = re.match(xml);

                if (m.hasMatch())
                    opfPath = m.captured(1);
            }
        }

        if (opfPath.isEmpty()) {
            qWarning() << "BookManager: no OPF found in EPUB";
            return {};
        }

        QStringList spineHrefs;
        QHash<QString, QString> manifest;
        QString tocHref;

        {
            QFile of(extractDir + opfPath);
            if (!of.open(QIODevice::ReadOnly))
                return {};

            const QString xml = QString::fromUtf8(of.readAll());
            const QString opfDir = opfPath.contains("/")
                                       ? opfPath.left(opfPath.lastIndexOf('/') + 1)
                                       : "";

            const QRegularExpression itemRe("<item\\b([^>]*)>", QRegularExpression::CaseInsensitiveOption);
            auto it = itemRe.globalMatch(xml);

            while (it.hasNext()) {
                auto m = it.next();

                const QString attrs = m.captured(1);
                const QString id   = QRegularExpression("id=['\"]([^'\"]+)['\"]").match(attrs).captured(1);
                const QString href = QRegularExpression("href=['\"]([^'\"]+)['\"]").match(attrs).captured(1);

                if (!id.isEmpty() && !href.isEmpty())
                    manifest[id] = href;
            }

            const QRegularExpression spineRe(
                "<itemref\\b[^>]*idref=['\"]([^'\"]+)['\"]",
                QRegularExpression::CaseInsensitiveOption
                );

            auto sit = spineRe.globalMatch(xml);

            while (sit.hasNext()) {
                auto m = sit.next();
                const QString id = m.captured(1);

                if (manifest.contains(id))
                    spineHrefs << (opfDir + manifest[id]);
            }

            for (auto it2 = manifest.begin(); it2 != manifest.end(); ++it2) {
                const QString href = it2.value();

                if (href.contains("nav", Qt::CaseInsensitive)
                    || href.contains("ncx", Qt::CaseInsensitive)
                    || href.contains("toc", Qt::CaseInsensitive)) {
                    tocHref = opfDir + href;
                    break;
                }
            }
        }

        if (spineHrefs.isEmpty()) {
            qWarning() << "BookManager: empty spine in EPUB";
            return {};
        }

        QJsonArray tocJson;

        if (!tocHref.isEmpty()) {
            QFile tf(extractDir + tocHref);

            if (tf.open(QIODevice::ReadOnly)) {
                const QString txml = QString::fromUtf8(tf.readAll());

                const QRegularExpression liRe(
                    "<li[^>]*>([\\s\\S]*?)</li>",
                    QRegularExpression::CaseInsensitiveOption
                    );

                auto lit = liRe.globalMatch(txml);

                while (lit.hasNext()) {
                    auto m = lit.next();
                    const QString inner = m.captured(1);

                    const QRegularExpression aRe(
                        "<a[^>]*href=['\"]([^'\"]+)['\"][^>]*>([\\s\\S]*?)</a>",
                        QRegularExpression::CaseInsensitiveOption
                        );

                    auto am = aRe.match(inner);

                    if (am.hasMatch()) {
                        QJsonObject item;
                        item["href"]  = am.captured(1);
                        item["label"] = am.captured(2).remove(QRegularExpression("<[^>]+>")).trimmed();
                        tocJson.append(item);
                    }
                }

                if (tocJson.isEmpty()) {
                    const QRegularExpression npRe(
                        "<navPoint[^>]*>([\\s\\S]*?)</navPoint>",
                        QRegularExpression::CaseInsensitiveOption
                        );

                    auto nit = npRe.globalMatch(txml);

                    while (nit.hasNext()) {
                        auto m = nit.next();
                        const QString inner = m.captured(1);

                        const QString src = QRegularExpression(
                                                "<content[^>]*src=['\"]([^'\"]+)['\"]",
                                                QRegularExpression::CaseInsensitiveOption
                                                ).match(inner).captured(1);

                        const QString lbl = QRegularExpression(
                                                "<text[^>]*>([\\s\\S]*?)</text>",
                                                QRegularExpression::CaseInsensitiveOption
                                                ).match(inner).captured(1);

                        if (!src.isEmpty()) {
                            QJsonObject item;
                            item["href"]  = src;
                            item["label"] = lbl.trimmed();
                            tocJson.append(item);
                        }
                    }
                }
            }
        }

        const QString savedLoc        = lastLocation(bookId);
        const auto    savedSetts      = readerSettings(bookId);

        const QString savedTheme      = savedSetts.value("theme", "sepia").toString();
        const int     savedFont       = qBound(60, savedSetts.value("fontSize", 140).toInt(), 200);
        const int     savedPadding    = qBound(-120, savedSetts.value("padding", -4).toInt(), 160);
        const QString savedTextColor  = savedSetts.value("textColor", "").toString();
        const int     savedBrightness = qBound(30, savedSetts.value("brightness", 100).toInt(), 200);
        const QString savedFontFamily = savedSetts.value("fontFamily", "Verdana").toString();
        const int     savedLineSpacing = qBound(100, savedSetts.value("lineSpacing", 220).toInt(), 300);

        QVariantList hlList = getHighlightsForBook(bookId);
        QJsonArray hlArray;
        for (const QVariant &v : std::as_const(hlList)) {
            hlArray.append(QJsonObject::fromVariantMap(v.toMap()));
        }

        QJsonArray spineJson;
        for (const QString &h : std::as_const(spineHrefs))
            spineJson.append(h);

        const QString htmlPath = extractDir + "reader_v3.html";

        QFile f(htmlPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning() << "BookManager: failed to write reader HTML:" << f.errorString();
            return {};
        }

        QString html;

        html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">\n");
        html += QStringLiteral("<style>\n");
        html += QStringLiteral("*{margin:0;padding:0;box-sizing:border-box}\n");
        html += QStringLiteral("html,body{width:100%;height:100%;overflow:hidden}\n");
        html += QStringLiteral("body,.chapter,.chapter *{-webkit-user-select:text;user-select:text}\n");
        html += QStringLiteral("body{background:#1a1a1a;display:flex;flex-direction:column;height:100vh}\n");
        html += QStringLiteral("#main{display:flex;flex:1;min-height:0;overflow:hidden;position:relative}\n");

        html += QStringLiteral(
            "#toc{position:absolute;top:0;left:0;bottom:0;width:280px;background:#141414;"
            "border-right:1px solid #2a2a2a;overflow-y:auto;z-index:50;"
            "transform:translateX(-100%);transition:transform 0.22s cubic-bezier(.4,0,.2,1);"
            "padding:8px 0 24px 0}\n"
            );

        html += QStringLiteral("#toc::-webkit-scrollbar{width:5px}\n");
        html += QStringLiteral("#toc::-webkit-scrollbar-track{background:transparent}\n");
        html += QStringLiteral("#toc::-webkit-scrollbar-thumb{background:rgba(79,195,247,0.25);border-radius:10px}\n");
        html += QStringLiteral("#toc::-webkit-scrollbar-thumb:hover{background:rgba(79,195,247,0.65)}\n");

        html += QStringLiteral(
            "#toc-header{padding:14px 16px 10px;font-family:Consolas,monospace;font-size:11px;"
            "font-weight:bold;color:#555;letter-spacing:1.5px;text-transform:uppercase;"
            "border-bottom:1px solid #222;margin-bottom:4px}\n"
            );

        html += QStringLiteral(
            ".toc-item{padding:9px 12px;cursor:pointer;font-family:Consolas,monospace;"
            "font-size:13px;color:#999;border:none;background:transparent;width:100%;"
            "text-align:left;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;"
            "display:block;transition:color 0.12s,background 0.12s;border-left:3px solid transparent}\n"
            );

        html += QStringLiteral(".toc-item:hover{color:#e0e0e0;background:#1e1e1e}\n");

        html += QStringLiteral(
            ".toc-item.active{color:#4fc3f7!important;"
            "background:linear-gradient(90deg,rgba(79,195,247,0.1) 0%,transparent 100%)!important;"
            "border-left:3px solid #4fc3f7!important;font-weight:bold}\n"
            );

        html += QStringLiteral(
            "#scroll-container{flex:1;overflow-y:auto;overflow-x:hidden;scroll-behavior:smooth;"
            "padding:0 0 60px;line-height:1.8}\n"
            );

        html += QStringLiteral("#scroll-container::-webkit-scrollbar{width:5px}\n");
        html += QStringLiteral("#scroll-container::-webkit-scrollbar-track{background:transparent}\n");
        html += QStringLiteral("#scroll-container::-webkit-scrollbar-thumb{background:rgba(79,195,247,0.25);border-radius:10px}\n");
        html += QStringLiteral("#scroll-container::-webkit-scrollbar-thumb:hover{background:rgba(79,195,247,0.65)}\n");

        html += QStringLiteral(".chapter{max-width:750px;margin:0 auto;padding:2em 0}\n");
        html += QStringLiteral(".chapter img{max-width:100%;height:auto;display:block;margin:1em auto}\n");
        html += QStringLiteral(".chapter-divider{border:none;border-top:1px solid #2a2a2a;margin:3em auto;width:60%}\n");

        html += QStringLiteral(
            "#status{color:#888;font-family:Consolas,monospace;padding:24px;font-size:14px;"
            "position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);pointer-events:none}\n"
            );

        html += QStringLiteral(
            "#error{display:none;color:#f55;font-family:Consolas,monospace;padding:24px;"
            "white-space:pre-wrap;font-size:14px;overflow:auto;height:100%}\n"
            );

        html += QStringLiteral(".search-highlight{background:rgba(255,200,0,0.4);color:#000;border-radius:2px}\n");

        html += QStringLiteral(".epub-hl{background-color:var(--hl-bg,#ffe082);color:#000!important;border-radius:2px;padding:0 1px;display:inline;line-height:inherit;vertical-align:baseline;cursor:pointer;transition:filter 0.15s ease}\n");
        html += QStringLiteral(".epub-hl:hover{filter:brightness(0.9)}\n");
        html += QStringLiteral("#hl-toolbar{position:fixed;z-index:2147483647;display:none;background:#1e1e1e;border:1px solid #333;border-radius:20px;padding:4px 8px;box-shadow:0 8px 24px rgba(0,0,0,0.6);flex-direction:row;align-items:center;gap:6px;transform:translate(-50%,-100%);margin-top:-8px;pointer-events:auto;user-select:none;-webkit-user-select:none}\n");
        html += QStringLiteral(".hl-btn{width:22px;height:22px;border-radius:11px;border:2px solid transparent;cursor:pointer;transition:transform 0.1s}\n");
        html += QStringLiteral(".hl-btn:hover{transform:scale(1.15)}\n");
        html += QStringLiteral(".hl-trash-btn{background:transparent;border:none;color:#ff5252;font-size:14px;cursor:pointer;padding:0 6px;display:flex;align-items:center}\n");

        html += QStringLiteral("</style></head><body>\n");

        html += QStringLiteral("<div id=\"main\">\n");
        html += QStringLiteral("  <div id=\"toc\"><div id=\"toc-header\">Contents</div></div>\n");
        html += QStringLiteral("  <div id=\"scroll-container\">\n");
        html += QStringLiteral("    <div id=\"status\">Loading book...</div>\n");
        html += QStringLiteral("    <div id=\"error\"></div>\n");
        html += QStringLiteral("  </div>\n");
        html += QStringLiteral("</div>\n");

        html += QStringLiteral("<div id=\"hl-toolbar\">\n");
        html += QStringLiteral("  <div class=\"hl-btn\" style=\"background:#ffe082\" data-color=\"#ffe082\"></div>\n");
        html += QStringLiteral("  <div class=\"hl-btn\" style=\"background:#a5d6a7\" data-color=\"#a5d6a7\"></div>\n");
        html += QStringLiteral("  <div class=\"hl-btn\" style=\"background:#81d4fa\" data-color=\"#81d4fa\"></div>\n");
        html += QStringLiteral("  <div class=\"hl-btn\" style=\"background:#f48fb1\" data-color=\"#f48fb1\"></div>\n");
        html += QStringLiteral("  <button id=\"hl-remove\" class=\"hl-trash-btn\" style=\"display:none;\">🗑</button>\n");
        html += QStringLiteral("</div>\n");

        html += QStringLiteral("<script>\n");
        html += "var SPINE=" + QJsonDocument(spineJson).toJson(QJsonDocument::Compact) + ";\n";
        html += "var TOC="   + QJsonDocument(tocJson).toJson(QJsonDocument::Compact)   + ";\n";
        html += "var SAVED_HIGHLIGHTS=" + QJsonDocument(hlArray).toJson(QJsonDocument::Compact) + ";\n";

        html += "var SAVED_THEME=\""      + savedTheme      + "\";\n";
        html += "var SAVED_FONTSIZE="     + QString::number(savedFont) + ";\n";
        html += "var SAVED_LOC=\""        + savedLoc        + "\";\n";
        html += "var SAVED_PADDING="      + QString::number(savedPadding) + ";\n";
        html += "var SAVED_TEXTCOLOR=\""  + savedTextColor  + "\";\n";
        html += "var SAVED_BRIGHTNESS="   + QString::number(savedBrightness) + ";\n";
        html += "var SAVED_FONTFAMILY=\"" + savedFontFamily + "\";\n";
        html += "var SAVED_LINESPACING=" + QString::number(savedLineSpacing) + ";\n";
        html += "var BOOK_ID="            + QString::number(bookId) + ";\n";
        html += "var BASE_URL=\""         + QUrl::fromLocalFile(extractDir).toString() + "\";\n";

        html += R"JS(
var statusEl = document.getElementById("status");

function setStatus(m) {
    if (statusEl) statusEl.textContent = m;
}

function showErr(m) {
    if (statusEl) statusEl.style.display = "none";
    var e = document.getElementById("error");
    if (e) {
        e.style.display = "block";
        e.textContent = m;
    }
}

window.onerror = function(m, u, l) {
    showErr("JS error: " + m + " (line " + l + ")");
};

window.addEventListener("unhandledrejection", function(ev) {
    var r = ev.reason;
    showErr("Rejected: " + (r && r.message ? r.message : String(r)));
});

function getStoredInt(key, fallback) {
    var v = sessionStorage.getItem(key);
    if (v === null || v === undefined || v === "") return fallback;

    var n = parseInt(v, 10);
    if (isNaN(n)) return fallback;

    return n;
}

var THEMES = {
    dark:  { bg:"#1a1a1a", fg:"#e0e0e0", link:"#4fc3f7", hr:"#2a2a2a" },
    light: { bg:"#ffffff", fg:"#222222", link:"#1565c0", hr:"#dddddd" },
    sepia: { bg:"#f4e4c1", fg:"#3b2a1a", link:"#8b4513", hr:"#c8a96e" }
};

var storedTheme = sessionStorage.getItem("epub_theme_" + BOOK_ID);
var _curTheme = (storedTheme === null || storedTheme === undefined || storedTheme === "")
    ? (SAVED_THEME || "sepia")
    : storedTheme;

var _curFontSz = getStoredInt("epub_fontSize_" + BOOK_ID, SAVED_FONTSIZE || 140);

var _curPadding = getStoredInt(
    "epub_padding_" + BOOK_ID,
    (typeof SAVED_PADDING === "number" ? SAVED_PADDING : -4)
);

var storedTextColor = sessionStorage.getItem("epub_textcolor_" + BOOK_ID);
var _curTextColor = (storedTextColor === null || storedTextColor === undefined)
    ? (SAVED_TEXTCOLOR || "")
    : storedTextColor;

var _curBrightness = getStoredInt("epub_brightness_" + BOOK_ID, SAVED_BRIGHTNESS || 100);

var storedFont = sessionStorage.getItem("epub_font_" + BOOK_ID);
var _curFont = (storedFont === null || storedFont === undefined)
? (SAVED_FONTFAMILY || "Verdana")
: storedFont;

var _curLineSpacing = getStoredInt(
    "epub_lineSpacing_" + BOOK_ID,
    SAVED_LINESPACING || 220
);
function applyTheme(name) {
    _curTheme = name || "sepia";
    var t = THEMES[_curTheme] || THEMES.sepia;
    document.body.style.background = t.bg;

    var fg = _curTextColor || t.fg;

    var st = document.getElementById("_th");
    if (!st) {
        st = document.createElement("style");
        st.id = "_th";
        document.head.appendChild(st);
    }

    st.textContent =
        "body,#scroll-container{background:" + t.bg + "!important}\n" +
        "#scroll-container,#scroll-container *:not(.epub-hl):not(.search-highlight):not(a){color:" + fg + "!important}\n" +
        "#scroll-container a{color:" + t.link + "!important}\n" +
        ".chapter-divider{border-top-color:" + t.hr + "!important}";
}

function applyFontSize(pct) {
    var sc = document.getElementById("scroll-container");
    if (sc) sc.style.fontSize = pct + "%";
}

function applyPadding(px) {
    var n = parseInt(px, 10);
    if (isNaN(n)) n = 0;

    n = Math.max(-120, Math.min(160, n));
    _curPadding = n;

    var st = document.getElementById("_margin_style");
    if (!st) {
        st = document.createElement("style");
        st.id = "_margin_style";
        document.head.appendChild(st);
    }

    if (n < 0) {
        var extra = Math.abs(n) * 12;
        var wide = 750 + extra;

        st.textContent =
            ".chapter{" +
            "width:100%;" +
            "max-width:min(100%, " + wide + "px);" +
            "margin:0 auto;" +
            "padding:2em 0;" +
            "}";
    } else {
        var maxW = Math.max(240, 750 - 2 * n);
        var side = 2 * n;

        st.textContent =
            ".chapter{" +
            "width:100%;" +
            "max-width:min(" + maxW + "px, calc(100% - " + side + "px));" +
            "margin:0 auto;" +
            "padding:2em 0;" +
            "}";
    }
}

function applyTextColor(color) {
    _curTextColor = color;
    var t = THEMES[_curTheme] || THEMES.sepia;
    var fg = _curTextColor || t.fg;

    var st = document.getElementById("_th");
    if (!st) {
        st = document.createElement("style");
        st.id = "_th";
        document.head.appendChild(st);
    }

    st.textContent =
        "body,#scroll-container{background:" + t.bg + "!important}\n" +
        "#scroll-container,#scroll-container *:not(.epub-hl):not(.search-highlight):not(a){color:" + fg + "!important}\n" +
        "#scroll-container a{color:" + t.link + "!important}\n" +
        ".chapter-divider{border-top-color:" + t.hr + "!important}";
}

function applyBrightness(pct) {
    var sc = document.getElementById("scroll-container");
    if (sc) sc.style.filter = "brightness(" + pct + "%)";
}

function applyFont(family) {
    var sc = document.getElementById("scroll-container");

    if (sc) {
        sc.style.fontFamily = family
            ? family + ",Georgia,serif"
            : "Verdana,Georgia,serif";
    }
}

function applyLineSpacing(pct) {
    var n = parseInt(pct, 10);

    if (isNaN(n))
        n = 220;

    n = Math.max(100, Math.min(300, n));

    _curLineSpacing = n;

    var st = document.getElementById("_line_spacing");

    if (!st) {
        st = document.createElement("style");
        st.id = "_line_spacing";
        document.head.appendChild(st);
    }

    st.textContent =
        "#scroll-container{line-height:" + n + "% !important}" +
        ".chapter,.chapter p,.chapter div,.chapter span,.chapter li,.chapter blockquote{line-height:" + n + "% !important}";

    var sc = document.getElementById("scroll-container");

    if (sc)
        sc.style.lineHeight = n + "%";
}
var tocEl = document.getElementById("toc");
var tocOpen = false;
var tocItems = [];
var chapterAnchors = {};

function normHref(href) {
    if (!href) return "";
    return href.split("#")[0].replace(/^.*\//, "").toLowerCase();
}

function buildToc(tocData) {
    var header = document.getElementById("toc-header");

    tocEl.innerHTML = "";
    if (header) tocEl.appendChild(header);

    tocItems = [];

    tocData.forEach(function(item) {
        var btn = document.createElement("button");
        btn.className = "toc-item";
        btn.textContent = item.label || "";
        btn._norm = normHref(item.href);

        btn.onclick = function() {
            var anchor = chapterAnchors[btn._norm];
            if (anchor) anchor.scrollIntoView({ behavior: "smooth", block: "start" });
        };

        tocEl.appendChild(btn);
        tocItems.push(btn);
    });
}

function updateActiveToc() {
    var sc = document.getElementById("scroll-container");
    if (!sc || !tocItems.length) return;

    var mid = sc.scrollTop + sc.clientHeight * 0.3;
    var best = null;
    var bestTop = -Infinity;

    Object.keys(chapterAnchors).forEach(function(norm) {
        var top = chapterAnchors[norm].offsetTop;
        if (top <= mid && top > bestTop) {
            bestTop = top;
            best = norm;
        }
    });

    tocItems.forEach(function(btn) {
        btn.classList.toggle("active", btn._norm === best);
    });
}

window.toggleToc = function() {
    tocOpen = !tocOpen;
    if (tocEl) {
        tocEl.style.transform = tocOpen ? "translateX(0)" : "translateX(-100%)";
    }
};

document.addEventListener("click", function(e) {
    if (tocOpen && tocEl && !tocEl.contains(e.target)) {
        tocOpen = false;
        tocEl.style.transform = "translateX(-100%)";
    }
});

document.addEventListener("click", function(e) {
    if (e.target.closest && e.target.closest("#toc")) return;

    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(JSON.stringify({ type: "clickOutside" }));
    }
});

var hlToolbar = document.getElementById("hl-toolbar");
var currentSelectionRange = null;
var activeHighlightEl = null;

if (hlToolbar) {
    hlToolbar.addEventListener("mousedown", function(e) {
        e.preventDefault();
        e.stopPropagation();
    });
    hlToolbar.addEventListener("mouseup", function(e) {
        e.preventDefault();
        e.stopPropagation();
    });
    hlToolbar.addEventListener("click", function(e) {
        e.preventDefault();
        e.stopPropagation();
    });
}

function updateToolbarPosition(rect) {
    if (!hlToolbar || !rect) return;
    hlToolbar.style.position = "fixed";
    hlToolbar.style.left = Math.max(80, Math.min(window.innerWidth - 80, rect.left + rect.width / 2)) + "px";
    hlToolbar.style.top  = Math.max(12, rect.top) + "px";
    hlToolbar.style.display = "flex";
}

function checkAndShowSelectionToolbar() {
    var sel = window.getSelection();
    if (!sel || sel.isCollapsed || !sel.toString().trim()) {
        if (!activeHighlightEl) hideHlToolbar();
        return;
    }

    var range = sel.getRangeAt(0);
    currentSelectionRange = range.cloneRange();
    var rect = range.getBoundingClientRect();

    if (rect.width === 0 && rect.height === 0) return;

    updateToolbarPosition(rect);

    var remBtn = document.getElementById("hl-remove");
    if (remBtn) remBtn.style.display = "none";
}

document.addEventListener("mouseup", function(e) {
    if (e.target.closest && e.target.closest("#hl-toolbar")) return;
    setTimeout(checkAndShowSelectionToolbar, 20);
});

document.addEventListener("keyup", function(e) {
    setTimeout(checkAndShowSelectionToolbar, 20);
});

function hideHlToolbar() {
    if (hlToolbar) hlToolbar.style.display = "none";
    activeHighlightEl = null;
    currentSelectionRange = null;
}

function safeHighlightRange(range, color, locId) {
    var selectedText = range.toString().trim();
    if (!selectedText) return false;

    var container = range.commonAncestorContainer;
    var textNodes = [];

    if (container.nodeType === Node.TEXT_NODE) {
        textNodes.push(container);
    } else {
        var walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT, {
            acceptNode: function(node) {
                return range.intersectsNode(node) ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
            }
        }, false);

        while (walker.nextNode()) {
            textNodes.push(walker.currentNode);
        }
    }

    if (textNodes.length === 0) return false;

    textNodes.forEach(function(node, index) {
        var isStart = (node === range.startContainer);
        var isEnd   = (node === range.endContainer);

        var startOffset = isStart ? range.startOffset : 0;
        var endOffset   = isEnd   ? range.endOffset   : node.nodeValue.length;

        if (startOffset >= endOffset) return;

        var sliceText = node.nodeValue.substring(startOffset, endOffset);
        if (!sliceText || /^\s*$/.test(sliceText)) return;

        var targetNode = node;

        if (startOffset > 0) {
            targetNode = node.splitText(startOffset);
        }

        var highlightLen = endOffset - startOffset;
        if (highlightLen < targetNode.nodeValue.length) {
            targetNode.splitText(highlightLen);
        }

        var mark = document.createElement("mark");
        mark.className = "epub-hl";
        mark.style.setProperty("--hl-bg", color);
        mark.id = locId + (index > 0 ? "_" + index : "");
        mark.dataset.hlGroup = locId;

        targetNode.parentNode.insertBefore(mark, targetNode);
        mark.appendChild(targetNode);
    });

    return true;
}

function getEpubPageAndChapter(range) {
    var sc = document.getElementById("scroll-container");
    var pageNum = 1;
    var chName = "";

    if (sc) {
        var topPos = sc.scrollTop;
        if (range) {
            try {
                var rect = range.getBoundingClientRect();
                var scRect = sc.getBoundingClientRect();
                topPos = sc.scrollTop + (rect.top - scRect.top);
            } catch(e) {}
        }
        var totalH = sc.scrollHeight - sc.clientHeight;
        var pct = totalH > 0 ? Math.max(0, Math.min(1, topPos / totalH)) : 0;
        var estimatedTotalPages = Math.max(20, (typeof SPINE !== "undefined" && SPINE) ? SPINE.length * 15 : 100);
        pageNum = Math.max(1, Math.round(pct * estimatedTotalPages));
    }

    if (typeof tocItems !== "undefined" && tocItems && tocItems.length) {
        for (var i = 0; i < tocItems.length; i++) {
            if (tocItems[i].classList.contains("active")) {
                chName = (tocItems[i].textContent || "").trim();
                break;
            }
        }
    }

    return "Page " + pageNum;
}

document.querySelectorAll(".hl-btn").forEach(function(btn) {
    btn.addEventListener("mousedown", function(e) {
        e.preventDefault();
        e.stopPropagation();
    });

    btn.onclick = function(e) {
        e.preventDefault();
        e.stopPropagation();

        var color = btn.getAttribute("data-color");

        if (activeHighlightEl) {
            var groupId = activeHighlightEl.dataset.hlGroup || activeHighlightEl.id;
            var targets = groupId ? document.querySelectorAll(".epub-hl[data-hl-group='" + groupId + "'], #" + groupId) : [activeHighlightEl];
            targets.forEach(function(el) {
                el.style.setProperty("--hl-bg", color);
            });
            hideHlToolbar();

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    type: "updateHighlightColor",
                    locator: groupId,
                    color: color
                }));
            }
            return;
        }

        if (!currentSelectionRange) return;

        var selectedText = currentSelectionRange.toString().trim();
        if (!selectedText) return;

        var locId = "hl_" + Date.now();
        var success = safeHighlightRange(currentSelectionRange, color, locId);
        var pageChapterStr = getEpubPageAndChapter(currentSelectionRange);

        window.getSelection().removeAllRanges();
        hideHlToolbar();

        if (success && window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({
                type: "addHighlight",
                text: selectedText,
                color: color,
                locator: locId,
                chapter: pageChapterStr
            }));
        }
    };
});

document.addEventListener("click", function(e) {
    var mark = e.target.closest(".epub-hl");
    if (mark) {
        e.stopPropagation();
        activeHighlightEl = mark;
        var rect = mark.getBoundingClientRect();
        updateToolbarPosition(rect);

        var remBtn = document.getElementById("hl-remove");
        if (remBtn) remBtn.style.display = "block";
    } else if (!e.target.closest("#hl-toolbar")) {
        var sel = window.getSelection();
        if (!sel || sel.isCollapsed || !sel.toString().trim()) {
            hideHlToolbar();
        }
    }
});

var remBtn = document.getElementById("hl-remove");
if (remBtn) {
    remBtn.addEventListener("mousedown", function(e) {
        e.preventDefault();
        e.stopPropagation();
    });

    remBtn.onclick = function(e) {
        e.preventDefault();
        e.stopPropagation();
        if (activeHighlightEl) {
            var groupId = activeHighlightEl.dataset.hlGroup || activeHighlightEl.id;
            var targets = groupId ? document.querySelectorAll(".epub-hl[data-hl-group='" + groupId + "'], #" + groupId) : [activeHighlightEl];
            var hlDbId = activeHighlightEl.dataset.hlId;
            var locId = groupId || activeHighlightEl.id;

            targets.forEach(function(el) {
                var parent = el.parentNode;
                while (el.firstChild) parent.insertBefore(el.firstChild, el);
                parent.removeChild(el);
                parent.normalize();
            });

            hideHlToolbar();

            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({
                    type: "removeHighlight",
                    id: hlDbId ? parseInt(hlDbId, 10) : 0,
                    locator: locId
                }));
            }
        }
    };
}

window.goToHighlight = function(locator) {
    if (!locator) return;
    var el = document.getElementById(locator) || document.querySelector(".epub-hl[data-hl-group='" + locator + "']");
    if (el) {
        el.scrollIntoView({ behavior: "smooth", block: "center" });
        el.style.filter = "brightness(1.5)";
        setTimeout(function() { el.style.filter = "none"; }, 1500);
    }
};

function normStr(s) {
    if (!s) return "";
    return s.replace(/[\u00a0\s]+/g, " ")
            .replace(/[\u2018\u2019\u201b]/g, "'")
            .replace(/[\u201c\u201d\u201f]/g, '"')
            .replace(/[\u2013\u2014]/g, "-")
            .trim();
}

function unwrapObsoleteHighlights(validHighlights) {
    var validGroups = new Set();
    validHighlights.forEach(function(h) {
        validGroups.add(h.locator || ("hl_" + h.id));
        if (h.id) validGroups.add(String(h.id));
    });

    var marks = document.querySelectorAll(".epub-hl");
    marks.forEach(function(el) {
        var grp = el.dataset.hlGroup || el.id;
        var hlId = el.dataset.hlId;
        if ((grp && !validGroups.has(grp)) && (hlId && !validGroups.has(String(hlId)))) {
            var parent = el.parentNode;
            if (parent) {
                while (el.firstChild) parent.insertBefore(el.firstChild, el);
                parent.removeChild(el);
                parent.normalize();
            }
        }
    });
}

// Instant Chapter-Filtered Highlight Restorer
window.loadHighlights = function(highlights) {
    if (!highlights || !highlights.length) {
        unwrapObsoleteHighlights([]);
        return;
    }
    window._savedHighlights = highlights;
    unwrapObsoleteHighlights(highlights);

    var sc = document.getElementById("scroll-container");
    if (!sc) return;

    var pending = highlights.filter(function(hl) {
        var groupLoc = hl.locator || ("hl_" + hl.id);
        return !(document.querySelector(".epub-hl[data-hl-id='" + hl.id + "']") ||
                 document.querySelector(".epub-hl[data-hl-group='" + groupLoc + "']") ||
                 document.getElementById(groupLoc));
    });

    if (!pending.length) return;

    var schedule = window.requestIdleCallback || function(cb) { setTimeout(cb, 10); };

    schedule(function() {
        var chapters = Array.from(document.querySelectorAll(".chapter"));
        if (!chapters.length) chapters = [sc];

        chapters.forEach(function(ch) {
            if (!ch._normTxt) {
                ch._normTxt = normStr(ch.textContent).toLowerCase();
            }
        });

        var blockTags = ["p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "blockquote", "section", "article", "br", "hr", "tr"];

        pending.forEach(function(hl) {
            var rawSearch = (hl.text || "").trim();
            var searchText = normStr(rawSearch);
            if (!searchText || searchText.length < 2) return;

            var searchLower = searchText.toLowerCase();

            chapters.forEach(function(ch) {
                if (!ch._normTxt.includes(searchLower)) {
                    if (searchText.length <= 25 || !ch._normTxt.includes(searchLower.substring(0, 25))) {
                        return;
                    }
                }

                highlightTextInContainer(ch, hl, searchText, searchLower, blockTags);
            });
        });
    });
};

function highlightTextInContainer(container, hl, searchText, searchLower, blockTags) {
    var walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT, {
        acceptNode: function(node) {
            var p = node.parentElement;
            if (!p) return NodeFilter.FILTER_REJECT;
            var tag = (p.tagName && typeof p.tagName === "string") ? p.tagName.toLowerCase() : "";
            if (tag === "script" || tag === "style" || p.closest(".epub-hl") || p.closest("#hl-toolbar")) return NodeFilter.FILTER_REJECT;
            return NodeFilter.FILTER_ACCEPT;
        }
    }, false);

    var nodes = [];
    var fullText = "";
    var lastBlockParent = null;

    var node;
    while ((node = walker.nextNode())) {
        var p = node.parentElement;
        var blockParent = p ? p.closest(blockTags.join(",")) : null;

        if (lastBlockParent && blockParent !== lastBlockParent && fullText.length > 0 && !/\s$/.test(fullText)) {
            fullText += " ";
        }
        lastBlockParent = blockParent;

        var val = node.nodeValue;
        nodes.push({ node: node, start: fullText.length, end: fullText.length + val.length });
        fullText += val;
    }

    if (!fullText) return;

    var normFull = normStr(fullText);
    var normFullLower = normFull.toLowerCase();

    var matchPos = normFullLower.indexOf(searchLower);
    var matchLen = searchText.length;

    if (matchPos === -1 && searchText.length > 25) {
        var sub = searchLower.substring(0, 25);
        matchPos = normFullLower.indexOf(sub);
        if (matchPos !== -1) {
            matchLen = Math.min(searchText.length, normFull.length - matchPos);
        }
    }

    if (matchPos === -1) return;

    var normToRaw = new Int32Array(fullText.length + 1);
    var normLen = 0;
    var inWs = false;

    for (var i = 0; i < fullText.length; i++) {
        var code = fullText.charCodeAt(i);
        var isWs = (code === 32 || code === 9 || code === 10 || code === 13 || code === 160);

        if (isWs) {
            if (!inWs) {
                normToRaw[normLen] = i;
                normLen++;
                inWs = true;
            }
        } else {
            inWs = false;
            normToRaw[normLen] = i;
            normLen++;
        }
    }
    normToRaw[normLen] = fullText.length;

    var rawStart = normToRaw[matchPos];
    var endIdx = Math.min(normLen, matchPos + matchLen);
    var rawEnd = (endIdx < normLen) ? normToRaw[endIdx] : fullText.length;

    if (rawStart === undefined || rawStart < 0) rawStart = 0;
    if (rawEnd === undefined || rawEnd > fullText.length) rawEnd = fullText.length;

    var locId = hl.locator || ("hl_" + hl.id);
    var markColor = hl.color || "#ffe082";

    nodes.forEach(function(nInfo, idx) {
        if (nInfo.end <= rawStart || nInfo.start >= rawEnd) return;
        var n = nInfo.node;
        var nodeStart = Math.max(0, rawStart - nInfo.start);
        var nodeEnd = Math.min(n.nodeValue.length, rawEnd - nInfo.start);

        if (nodeStart >= nodeEnd) return;

        var sliceText = n.nodeValue.substring(nodeStart, nodeEnd);
        if (!sliceText || /^\s*$/.test(sliceText)) return;

        var targetNode = n;
        if (nodeStart > 0) {
            targetNode = n.splitText(nodeStart);
        }
        if ((nodeEnd - nodeStart) < targetNode.nodeValue.length) {
            targetNode.splitText(nodeEnd - nodeStart);
        }

        var mark = document.createElement("mark");
        mark.className = "epub-hl";
        mark.style.setProperty("--hl-bg", markColor);
        mark.id = locId + (idx > 0 ? "_" + idx : "");
        mark.dataset.hlGroup = locId;
        mark.dataset.hlId = hl.id;

        targetNode.parentNode.insertBefore(mark, targetNode);
        mark.appendChild(targetNode);
    });
}

var _searchMatches = [];
var _searchIdx = -1;

window.searchInBook = function(query) {
    document.querySelectorAll(".search-highlight").forEach(function(el) {
        var parent = el.parentNode;
        parent.replaceChild(document.createTextNode(el.textContent), el);
        parent.normalize();
    });

    _searchMatches = [];
    _searchIdx = -1;

    if (!query || query.length < 2) {
        postSearchResult(0, 0);
        return;
    }

    var sc = document.getElementById("scroll-container");
    if (!sc) {
        postSearchResult(0, 0);
        return;
    }

    var walker = document.createTreeWalker(sc, NodeFilter.SHOW_TEXT, {
        acceptNode: function(node) {
            var p = node.parentElement;
            if (!p) return NodeFilter.FILTER_REJECT;

            var tag = p.tagName && p.tagName.toLowerCase();
            if (tag === "script" || tag === "style") return NodeFilter.FILTER_REJECT;

            return NodeFilter.FILTER_ACCEPT;
        }
    }, false);

    var textNodes = [];
    var node;

    while ((node = walker.nextNode())) {
        textNodes.push(node);
    }

    var esc = query.replace(/[.*+?^${}()|[\]\\]/g, function(c) {
        return String.fromCharCode(92) + c;
    });

    var re = new RegExp(esc, "gi");

    textNodes.forEach(function(tn) {
        var text = tn.textContent;
        if (!re.test(text)) return;

        re.lastIndex = 0;

        var frag = document.createDocumentFragment();
        var last = 0;
        var m;

        while ((m = re.exec(text)) !== null) {
            if (m.index > last) {
                frag.appendChild(document.createTextNode(text.slice(last, m.index)));
            }

            var mark = document.createElement("mark");
            mark.className = "search-highlight";
            mark.textContent = m[0];

            frag.appendChild(mark);
            _searchMatches.push(mark);

            last = re.lastIndex;
        }

        if (last < text.length) {
            frag.appendChild(document.createTextNode(text.slice(last)));
        }

        tn.parentNode.replaceChild(frag, tn);
    });

    postSearchResult(_searchMatches.length, 0);

    if (_searchMatches.length > 0) {
        _searchIdx = 0;
        scrollToMatch(0);
    }
};

window.searchNext = function() {
    if (!_searchMatches.length) return;

    _searchIdx = (_searchIdx + 1) % _searchMatches.length;
    scrollToMatch(_searchIdx);
    postSearchResult(_searchMatches.length, _searchIdx + 1);
};

window.searchPrev = function() {
    if (!_searchMatches.length) return;

    _searchIdx = (_searchIdx - 1 + _searchMatches.length) % _searchMatches.length;
    scrollToMatch(_searchIdx);
    postSearchResult(_searchMatches.length, _searchIdx + 1);
};

function scrollToMatch(idx) {
    var el = _searchMatches[idx];
    if (!el) return;

    _searchMatches.forEach(function(m, i) {
        m.style.background = i === idx ? "#ff0" : "rgba(255,200,0,0.4)";
        m.style.color = "#000";
    });

    el.scrollIntoView({ behavior: "smooth", block: "center" });
}

function postSearchResult(total, current) {
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(JSON.stringify({
            type: "searchResult",
            total: total,
            current: current
        }));
    }
}

window.setEpubTheme = function(t) {
    _curTheme = t;
    applyTheme(t);
    sessionStorage.setItem("epub_theme_" + BOOK_ID, t);
};

window.setEpubFontSize = function(p) {
    _curFontSz = p;
    applyFontSize(p);
    sessionStorage.setItem("epub_fontSize_" + BOOK_ID, p);
};

window.setEpubPadding = function(px) {
    applyPadding(px);
    sessionStorage.setItem("epub_padding_" + BOOK_ID, String(_curPadding));
};

window.setEpubTextColor = function(color) {
    _curTextColor = color;
    applyTextColor(color);
    sessionStorage.setItem("epub_textcolor_" + BOOK_ID, color);
};

window.setEpubBrightness = function(pct) {
    _curBrightness = pct;
    applyBrightness(pct);
    sessionStorage.setItem("epub_brightness_" + BOOK_ID, pct);
};

window.setEpubFont = function(family) {
    _curFont = family;
    applyFont(family);
    sessionStorage.setItem("epub_font_" + BOOK_ID, family);
};

window.setEpubLineSpacing = function(pct) {
    applyLineSpacing(pct);
    sessionStorage.setItem("epub_lineSpacing_" + BOOK_ID, String(_curLineSpacing));
};
window.setEpubFlow = function() {};

window.goToLocation = function(cfi) {
    if (!cfi || cfi.indexOf("scrollpct:") !== 0) return;

    var pct = parseFloat(cfi.replace("scrollpct:", ""));
    if (isNaN(pct)) return;

    var sc = document.getElementById("scroll-container");
    if (!sc) return;

    requestAnimationFrame(function() {
        requestAnimationFrame(function() {
            sc.scrollTop = pct * (sc.scrollHeight - sc.clientHeight);
        });
    });
};

var _scrollTimer = null;

function onScroll() {
    clearTimeout(_scrollTimer);

    _scrollTimer = setTimeout(function() {
        var sc = document.getElementById("scroll-container");
        if (!sc) return;

        var h = sc.scrollHeight - sc.clientHeight;
        var pct = h > 0 ? sc.scrollTop / h : 0;

        var estimatedTotalPages = Math.max(20, SPINE.length * 15);
        var estPage = Math.max(1, Math.round(pct * estimatedTotalPages));

        updateActiveToc();

        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({
                type: "progress",
                page: estPage,
                totalPages: estimatedTotalPages,
                pct: pct,
                cfi: "scrollpct:" + pct.toFixed(6)
            }));
        }
    }, 300);
}

function resolveUrl(base, rel) {
    if (!rel || rel.startsWith("http") || rel.startsWith("data:") || rel.startsWith("file:")) {
        return rel;
    }

    var frag = "";

    if (rel.includes("#")) {
        var parts = rel.split("#");
        rel = parts[0];
        frag = "#" + parts[1];
    }

    return new URL(rel, base).toString() + frag;
}

function rewriteUrls(html, chapterUrl) {
    return html
        .replace(/(src|href)\s*=\s*["']([^"'#?][^"']*?)["']/gi, function(match, attr, url) {
            if (url.startsWith("http") || url.startsWith("data:") || url.startsWith("file:")) {
                return match;
            }

            return attr + '="' + resolveUrl(chapterUrl, url) + '"';
        })
        .replace(/url\(\s*["']?([^"')]+)["']?\s*\)/gi, function(match, url) {
            if (url.startsWith("http") || url.startsWith("data:") || url.startsWith("file:")) {
                return match;
            }

            return 'url("' + resolveUrl(chapterUrl, url) + '")';
        });
}

var sc = document.getElementById("scroll-container");

if (!sc) {
    showErr("scroll-container not found");
} else if (!SPINE || !SPINE.length) {
    showErr("No chapters found in book spine");
} else {
    setStatus("Loading book...");

    function renderChapterNode(chPath, chUrl, html) {
        var bodyM = html.match(/<body[^>]*>([\s\S]*?)<\/body>/i);
        var body = bodyM ? bodyM[1] : html;
        body = rewriteUrls(body, chUrl);

        var norm = normHref(chPath);

        var article = document.createElement("article");
        article.className = "chapter";
        article.dataset.chapter = norm;
        article.innerHTML = body;

        chapterAnchors[norm] = article;
        return article;
    }

    function onAllChaptersLoaded() {
        if (statusEl) statusEl.style.display = "none";

applyTheme(_curTheme);
applyFontSize(_curFontSz);
applyPadding(_curPadding);
applyBrightness(_curBrightness);
applyLineSpacing(_curLineSpacing);
if (_curFont) applyFont(_curFont);
if (_curTextColor) applyTextColor(_curTextColor);
        sc.addEventListener("scroll", onScroll, { passive: true });

        if (SAVED_LOC && SAVED_LOC.indexOf("scrollpct:") === 0) {
            var pct = parseFloat(SAVED_LOC.replace("scrollpct:", ""));
            if (!isNaN(pct)) {
                requestAnimationFrame(function() {
                    requestAnimationFrame(function() {
                        sc.scrollTop = pct * (sc.scrollHeight - sc.clientHeight);
                    });
                });
            }
        }

        if (TOC && TOC.length) {
            buildToc(TOC);
        }

        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({ type: "epubLoaded" }));
        }

        if (typeof SAVED_HIGHLIGHTS !== "undefined" && SAVED_HIGHLIGHTS && SAVED_HIGHLIGHTS.length) {
            setTimeout(function() {
                window.loadHighlights(SAVED_HIGHLIGHTS);
            }, 300);
        }
    }

    var firstChPath = SPINE[0];
    var firstChUrl = resolveUrl(BASE_URL, firstChPath);

    fetch(firstChUrl).then(function(r) {
        if (!r.ok) throw new Error("Failed to load first chapter");
        return r.text();
    }).then(function(html) {
        var firstArticle = renderChapterNode(firstChPath, firstChUrl, html);
        if (statusEl) statusEl.style.display = "none";
        sc.appendChild(firstArticle);

applyTheme(_curTheme);
applyFontSize(_curFontSz);
applyPadding(_curPadding);
applyBrightness(_curBrightness);
applyLineSpacing(_curLineSpacing);
if (_curFont) applyFont(_curFont);
if (_curTextColor) applyTextColor(_curTextColor);

        if (SPINE.length <= 1) {
            onAllChaptersLoaded();
            return;
        }

        var remaining = SPINE.slice(1);
        var promises = remaining.map(function(chPath) {
            var chUrl = resolveUrl(BASE_URL, chPath);
            return fetch(chUrl)
                .then(function(r) {
                    if (!r.ok) return { chPath: chPath, html: "" };
                    return r.text().then(function(txt) {
                        return { chPath: chPath, chUrl: chUrl, html: txt };
                    });
                })
                .catch(function() {
                    return { chPath: chPath, html: "" };
                });
        });

        Promise.all(promises).then(function(results) {
            var frag = document.createDocumentFragment();

            results.forEach(function(item) {
                if (!item.html) return;
                var article = renderChapterNode(item.chPath, item.chUrl, item.html);
                var hr = document.createElement("hr");
                hr.className = "chapter-divider";
                frag.appendChild(hr);
                frag.appendChild(article);
            });

            sc.appendChild(frag);
            onAllChaptersLoaded();
        });
    }).catch(function(err) {
        showErr("EPUB load error: " + (err && err.message ? err.message : String(err)));
    });
}
)JS";

        html += QStringLiteral("</script></body></html>\n");

        f.write(html.toUtf8());
        f.close();

        qDebug() << "BookManager: EPUB reader written ->" << htmlPath;

        return QUrl::fromLocalFile(htmlPath).toString() + "?v=" + QString::number(QDateTime::currentMSecsSinceEpoch());
    }

    if (e.format == "CBZ") {
        const QString extractDir = appDataDir() + "/cbz_cache/" + QString::number(bookId) + "/";
        QDir().mkpath(extractDir);

        if (QDir(extractDir).isEmpty()) {
            extractZipArchive(e.filePath, extractDir);
        }

        QDir dir(extractDir);
        QStringList imgs = dir.entryList(
            {"*.jpg", "*.jpeg", "*.png", "*.webp", "*.gif"},
            QDir::Files,
            QDir::Name
            );

        QString imgTags;

        for (const QString &img : std::as_const(imgs)) {
            const QString imgUrl = QUrl::fromLocalFile(extractDir + img).toString();

            imgTags += QStringLiteral(
                           "<img src=\"%1\" style=\"display:block;width:100%%;margin:0 auto;\">\n"
                           ).arg(imgUrl);
        }

        const QString htmlPath = extractDir + "reader.html";

        const QString html = QStringLiteral(R"(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<style>
* { margin:0; padding:0; }
body { background:#111; overflow-y:auto; overflow-x:hidden; }
img { max-width:100vw; }
</style>
</head><body>
%1
</body></html>)").arg(imgTags);

        QFile f(htmlPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(html.toUtf8());

        return QUrl::fromLocalFile(htmlPath).toString();
    }

    return {};
}

// Covers

QString BookManager::coverOutputPath(int bookId) const
{
    return appDataDir() + "/book_covers/" + QString::number(bookId) + ".jpg";
}

void BookManager::ensureCovers()
{
    for (const BookEntry &e : std::as_const(m_entries)) {
        if (!isValidCoverFile(e.coverPath))
            extractCover(e);
    }
}

void BookManager::extractCover(const BookEntry &entry)
{
    if (entry.id == m_currentCoverId)
        return;

    for (const BookEntry &q : std::as_const(m_coverQueue)) {
        if (q.id == entry.id)
            return;
    }

    m_coverQueue.append(entry);

    if (!m_isExtractingCover)
        processNextCover();
}

void BookManager::processNextCover()
{
    if (m_coverQueue.isEmpty()) {
        m_isExtractingCover = false;
        m_currentCoverId = -1;
        return;
    }

    m_isExtractingCover = true;

    const BookEntry entry = m_coverQueue.takeFirst();
    m_currentCoverId = entry.id;

    if (entry.format == "PDF")
        extractPdfCover(entry);
    else if (entry.format == "EPUB")
        extractEpubCover(entry);
    else if (entry.format == "CBZ")
        extractCbzCover(entry);
    else
        scheduleNextCover();
}

void BookManager::scheduleNextCover()
{
    QTimer::singleShot(0, this, [this](){ processNextCover(); });
}

void BookManager::applyCoverPath(int bookId, const QString &coverPath)
{
    const int row = rowForId(bookId);
    if (row < 0)
        return;

    m_entries[row].coverPath = coverPath;
    updateCoverInDatabase(bookId, coverPath);

    emit dataChanged(index(row), index(row), { CoverPathRole });
}

QString BookManager::ghostscriptPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        "gswin64c.exe",
        "gswin32c.exe",
        "gswin64.exe",
        "gswin32.exe",
        "gs.exe",
        "ghostscript.exe"
    };

    for (const QString &name : std::as_const(candidates)) {
        const QString p = appDir + "/" + name;

        if (QFileInfo::exists(p))
            return QDir::toNativeSeparators(p);
    }

    for (const QString &name : std::as_const(candidates)) {
        const QString p = QStandardPaths::findExecutable(name);

        if (!p.isEmpty())
            return QDir::toNativeSeparators(p);
    }

    return {};
}

void BookManager::extractPdfCover(const BookEntry &entry)
{
    const QString outPath = coverOutputPath(entry.id);

    if (isValidCoverFile(outPath)) {
        applyCoverPath(entry.id, outPath);
        scheduleNextCover();
        return;
    }

    QDir().mkpath(QFileInfo(outPath).absolutePath());
    QFile::remove(outPath);

    const QString gs = ghostscriptPath();

    if (gs.isEmpty()) {
        qWarning() << "BookManager: Ghostscript not found next to exe or in PATH";
        scheduleNextCover();
        return;
    }

    QProcess *proc = new QProcess(this);

    const QStringList args = {
        "-dSAFER",
        "-dBATCH",
        "-dNOPAUSE",
        "-dNOPROMPT",
        "-dQUIET",
        "-dFirstPage=1",
        "-dLastPage=1",
        "-sDEVICE=jpeg",
        "-dJPEGQ=82",
        "-r45",
        "-sOutputFile=" + QDir::toNativeSeparators(outPath),
        QDir::toNativeSeparators(entry.filePath)
    };

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, entry, outPath, proc](int code, QProcess::ExitStatus st) {
                proc->deleteLater();

                if (st == QProcess::NormalExit && code == 0 && isValidCoverFile(outPath))
                    applyCoverPath(entry.id, outPath);

                scheduleNextCover();
            });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        proc->deleteLater();
        scheduleNextCover();
    });

    proc->start(gs, args);
}

void BookManager::extractEpubCover(const BookEntry &entry)
{
    const QString outPath = coverOutputPath(entry.id);

    if (isValidCoverFile(outPath)) {
        applyCoverPath(entry.id, outPath);
        scheduleNextCover();
        return;
    }

    QDir().mkpath(QFileInfo(outPath).absolutePath());
    QFile::remove(outPath);

    const QString extractDir = appDataDir() + "/epub_tmp/" + QString::number(entry.id) + "/";
    QDir().mkpath(extractDir);

    const int bookId = entry.id;

    if (extractZipArchive(entry.filePath, extractDir)) {
        QStringList images;

        QDirIterator it(
            extractDir,
            {"*.jpg", "*.jpeg", "*.png", "*.webp"},
            QDir::Files,
            QDirIterator::Subdirectories
            );

        while (it.hasNext())
            images << it.next();

        std::sort(images.begin(), images.end());

        QString found;

        for (const QString &p : std::as_const(images)) {
            const QString base = QFileInfo(p).fileName().toLower();

            if (base.contains("cover") || base.contains("page_001") || base.contains("0001")) {
                found = p;
                break;
            }
        }

        if (found.isEmpty() && !images.isEmpty())
            found = images.first();

        if (!found.isEmpty())
            QFile::copy(found, outPath);

        QDir(extractDir).removeRecursively();

        if (isValidCoverFile(outPath))
            applyCoverPath(bookId, outPath);

        scheduleNextCover();
    } else {
        QDir(extractDir).removeRecursively();
        scheduleNextCover();
    }
}

void BookManager::extractCbzCover(const BookEntry &entry)
{
    const QString outPath = coverOutputPath(entry.id);

    if (isValidCoverFile(outPath)) {
        applyCoverPath(entry.id, outPath);
        scheduleNextCover();
        return;
    }

    QDir().mkpath(QFileInfo(outPath).absolutePath());
    QFile::remove(outPath);

    const QString extractDir = appDataDir() + "/cbz_tmp/" + QString::number(entry.id) + "/";
    QDir().mkpath(extractDir);

    const int bookId = entry.id;

    if (extractZipArchive(entry.filePath, extractDir)) {
        const QStringList imgs = QDir(extractDir).entryList(
            {"*.jpg", "*.jpeg", "*.png", "*.webp"},
            QDir::Files,
            QDir::Name
            );

        if (!imgs.isEmpty())
            QFile::copy(extractDir + imgs.first(), outPath);

        QDir(extractDir).removeRecursively();

        if (isValidCoverFile(outPath))
            applyCoverPath(bookId, outPath);

        scheduleNextCover();
    } else {
        QDir(extractDir).removeRecursively();
        scheduleNextCover();
    }
}



QString BookManager::appDataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString BookManager::guessTitleFromFileName(const QString &fileName) const
{
    QString t = fileName;
    t.replace(QRegularExpression("[._-]"), " ");

    static const QRegularExpression year(R"(\b(19|20)\d{2}\b.*$)");
    t.remove(year);

    return t.trimmed();
}

int BookManager::rowForId(int bookId) const
{
    for (int i = 0; i < m_entries.count(); ++i) {
        if (m_entries.at(i).id == bookId)
            return i;
    }

    return -1;
}



void BookManager::openDatabase()
{
    const QString dataDir = appDataDir();

    QDir().mkpath(dataDir);
    QDir().mkpath(dataDir + "/book_covers");

    m_db = QSqlDatabase::addDatabase("QSQLITE", "book_connection");
    m_db.setDatabaseName(dataDir + "/books.db");

    if (m_db.open()) {
        QSqlQuery q(m_db);
        q.exec("PRAGMA journal_mode = WAL;");
        q.exec("PRAGMA cache_size = -2000;"); // 2MB memory cache limit for SQLite
    } else {
        qDebug() << "BookManager: failed to open DB:" << m_db.lastError().text();
    }
}

void BookManager::createTablesIfNeeded()
{
    QSqlQuery q(m_db);

    q.exec(R"(
CREATE TABLE IF NOT EXISTS books (
    id           INTEGER PRIMARY KEY,
    title        TEXT,
    author       TEXT,
    format       TEXT,
    file_path    TEXT,
    cover_path   TEXT,
    progress     REAL DEFAULT 0,
    current_page INTEGER DEFAULT 0,
    total_pages  INTEGER DEFAULT 0,
    sort_order   INTEGER DEFAULT 0,
    file_size    INTEGER DEFAULT 0
)
)");

    q.exec(R"(
CREATE TABLE IF NOT EXISTS highlights (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id     INTEGER NOT NULL,
    chapter     TEXT,
    locator     TEXT,
    text        TEXT NOT NULL,
    color       TEXT DEFAULT '#ffe082',
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(book_id) REFERENCES books(id) ON DELETE CASCADE
)
)");


    q.exec("CREATE INDEX IF NOT EXISTS idx_highlights_book_id ON highlights(book_id)");
}

void BookManager::loadFromDatabase()
{
    QSqlQuery q(m_db);

    q.exec(
        "SELECT id,title,author,format,file_path,cover_path,progress,"
        "current_page,total_pages,sort_order,file_size "
        "FROM books ORDER BY sort_order,id"
        );

    while (q.next()) {
        BookEntry e;

        e.id          = q.value(0).toInt();
        e.title       = q.value(1).toString();
        e.author      = q.value(2).toString();
        e.format      = q.value(3).toString();
        e.filePath    = q.value(4).toString();
        e.coverPath   = q.value(5).toString();
        e.progress    = q.value(6).toDouble();
        e.currentPage = q.value(7).toInt();
        e.totalPages  = q.value(8).toInt();
        e.sortOrder   = q.value(9).toInt();
        e.fileSize    = q.value(10).toLongLong();

        if (e.id >= m_nextId)
            m_nextId = e.id + 1;

        m_entries.append(e);
    }
}

void BookManager::saveToDatabase(const BookEntry &e)
{
    QSqlQuery q(m_db);

    q.prepare(
        "INSERT INTO books "
        "(id,title,author,format,file_path,cover_path,progress,"
        "current_page,total_pages,sort_order,file_size) "
        "VALUES(:id,:t,:a,:f,:fp,:cp,:pr,:cp2,:tp,:so,:fs)"
        );

    q.bindValue(":id",  e.id);
    q.bindValue(":t",   e.title);
    q.bindValue(":a",   e.author);
    q.bindValue(":f",   e.format);
    q.bindValue(":fp",  e.filePath);
    q.bindValue(":cp",  e.coverPath);
    q.bindValue(":pr",  e.progress);
    q.bindValue(":cp2", e.currentPage);
    q.bindValue(":tp",  e.totalPages);
    q.bindValue(":so",  e.sortOrder);
    q.bindValue(":fs",  e.fileSize);

    if (!q.exec())
        qDebug() << "BookManager: saveToDatabase failed:" << q.lastError().text();
}

// Reader settings

static QString settingsSidecarPath(const QString &appDataDir, int bookId)
{
    return appDataDir + "/readers/" + QString::number(bookId) + "_settings.json";
}

void BookManager::saveReaderSettings(
    int bookId,
    const QString &theme,
    int fontSize,
    const QString &flow,
    int padding,
    const QString &textColor,
    int brightness,
    const QString &fontFamily,
    int lineSpacing
    ) {
    QDir().mkpath(appDataDir() + "/readers/");

    const QString path = settingsSidecarPath(appDataDir(), bookId);

    QJsonObject obj;

    {
        QFile fr(path);
        if (fr.open(QIODevice::ReadOnly))
            obj = QJsonDocument::fromJson(fr.readAll()).object();
    }

    obj["theme"]       = theme;
    obj["fontSize"]    = qBound(60, fontSize, 200);
    obj["flow"]        = flow;
    obj["padding"]     = qBound(-120, padding, 160);
    obj["textColor"]   = textColor;
    obj["brightness"]  = qBound(30, brightness, 200);
    obj["fontFamily"]  = fontFamily;
    obj["lineSpacing"] = qBound(100, lineSpacing, 300);

    QFile fw(path);

    if (fw.open(QIODevice::WriteOnly))
        fw.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QVariantMap BookManager::readerSettings(int bookId) const
{
    QVariantMap out;

    QFile f(settingsSidecarPath(appDataDir(), bookId));

    if (!f.open(QIODevice::ReadOnly))
        return out;

    auto obj = QJsonDocument::fromJson(f.readAll()).object();

    if (obj.contains("theme"))
        out["theme"] = obj["theme"].toString();

    if (obj.contains("fontSize"))
        out["fontSize"] = obj["fontSize"].toInt();

    if (obj.contains("flow"))
        out["flow"] = obj["flow"].toString();

    if (obj.contains("padding"))
        out["padding"] = obj["padding"].toInt(-4);

    if (obj.contains("textColor"))
        out["textColor"] = obj["textColor"].toString();

    if (obj.contains("brightness"))
        out["brightness"] = obj["brightness"].toInt(100);

    if (obj.contains("fontFamily"))
        out["fontFamily"] = obj["fontFamily"].toString();

    if (obj.contains("lineSpacing"))
        out["lineSpacing"] = qBound(100, obj["lineSpacing"].toInt(220), 300);

    return out;
}

QString BookManager::lastLocation(int bookId) const
{
    QFile f(settingsSidecarPath(appDataDir(), bookId));

    if (!f.open(QIODevice::ReadOnly))
        return {};

    auto obj = QJsonDocument::fromJson(f.readAll()).object();
    return obj["location"].toString();
}

void BookManager::saveLocation(int bookId, const QString &cfi)
{
    QDir().mkpath(appDataDir() + "/readers/");

    const QString path = settingsSidecarPath(appDataDir(), bookId);

    QJsonObject obj;

    {
        QFile fr(path);
        if (fr.open(QIODevice::ReadOnly))
            obj = QJsonDocument::fromJson(fr.readAll()).object();
    }

    obj["location"] = cfi;

    QFile fw(path);

    if (fw.open(QIODevice::WriteOnly))
        fw.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void BookManager::savePdfBinaryFile(int bookId, const QString &base64Data)
{
    if (base64Data.isEmpty()) {
        qWarning() << "BookManager: Received empty base64 data for book ID:" << bookId;
        emit pdfSaved(bookId);
        return;
    }

    const int row = rowForId(bookId);
    if (row < 0) {
        qWarning() << "BookManager: Book ID not found:" << bookId;
        emit pdfSaved(bookId);
        return;
    }

    const QString originalFilePath = m_entries.at(row).filePath;
    if (originalFilePath.isEmpty()) {
        qWarning() << "BookManager: Original file path is empty for book ID:" << bookId;
        emit pdfSaved(bookId);
        return;
    }

    const QByteArray pdfBytes = QByteArray::fromBase64(base64Data.toUtf8());
    if (pdfBytes.size() < 100) {
        qWarning() << "BookManager: Decoded PDF bytes size is suspiciously small:" << pdfBytes.size();
        emit pdfSaved(bookId);
        return;
    }


    QFile file(originalFilePath);
    bool writeSuccess = false;

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (file.write(pdfBytes) == pdfBytes.size()) {
            writeSuccess = true;
        }
        file.close();
    }

    // Fallback
    if (!writeSuccess) {
        const QString tempSavePath = originalFilePath + ".tmp_save";
        QFile tempFile(tempSavePath);
        if (tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (tempFile.write(pdfBytes) == pdfBytes.size()) {
                tempFile.close();
                QFile::remove(originalFilePath);
                if (QFile::rename(tempSavePath, originalFilePath)) {
                    writeSuccess = true;
                }
            } else {
                tempFile.close();
            }
        }
        QFile::remove(tempSavePath);
    }

    if (writeSuccess) {
        qDebug() << "BookManager: Successfully saved native PDF annotations directly to:" << originalFilePath;
    } else {
        qWarning() << "BookManager: Failed to write updated PDF to original path:" << originalFilePath;
    }

    emit pdfSaved(bookId);
}

void BookManager::updateProgressInDatabase(int bookId, int currentPage, int totalPages, double progress)
{
    QSqlQuery q(m_db);

    q.prepare("UPDATE books SET current_page=:cp,total_pages=:tp,progress=:pr WHERE id=:id");

    q.bindValue(":cp", currentPage);
    q.bindValue(":tp", totalPages);
    q.bindValue(":pr", progress);
    q.bindValue(":id", bookId);

    q.exec();
}

void BookManager::updateCoverInDatabase(int bookId, const QString &coverPath)
{
    QSqlQuery q(m_db);

    q.prepare("UPDATE books SET cover_path=:cp WHERE id=:id");

    q.bindValue(":cp", coverPath);
    q.bindValue(":id", bookId);

    q.exec();
}

void BookManager::deleteFromDatabase(int bookId)
{
    QSqlQuery q(m_db);

    q.prepare("DELETE FROM books WHERE id=:id");
    q.bindValue(":id", bookId);

    q.exec();
}

// Font Management

QStringList BookManager::availableFonts() const
{
    if (m_cachedFonts.isEmpty()) {
        QFontDatabase db;
        m_cachedFonts = db.families();
    }
    return m_cachedFonts;
}

//  Highlights

int BookManager::addHighlight(
    int bookId,
    const QString &chapter,
    const QString &locator,
    const QString &text,
    const QString &color
    ) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO highlights (book_id, chapter, locator, text, color, created_at) VALUES (:b, :c, :l, :t, :col, :dt)");
    q.bindValue(":b", bookId);
    q.bindValue(":c", chapter);
    q.bindValue(":l", locator);
    q.bindValue(":t", text);
    q.bindValue(":col", color.isEmpty() ? "#ffe082" : color);
    q.bindValue(":dt", QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss"));

    if (q.exec()) {
        emit highlightsChanged();
        return q.lastInsertId().toInt();
    }
    qWarning() << "BookManager: addHighlight failed:" << q.lastError().text();
    return -1;
}

void BookManager::removeHighlight(int highlightId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM highlights WHERE id=:id");
    q.bindValue(":id", highlightId);
    if (q.exec()) {
        emit highlightsChanged();
    }
}

void BookManager::removeHighlightByLocator(const QString &locator)
{
    if (locator.isEmpty())
        return;

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM highlights WHERE locator=:l");
    q.bindValue(":l", locator);
    if (q.exec()) {
        emit highlightsChanged();
    }
}

void BookManager::updateHighlightColor(const QString &locator, const QString &color)
{
    if (locator.isEmpty())
        return;

    QSqlQuery q(m_db);
    q.prepare("UPDATE highlights SET color=:c WHERE locator=:l");
    q.bindValue(":c", color.isEmpty() ? "#ffe082" : color);
    q.bindValue(":l", locator);
    if (q.exec()) {
        emit highlightsChanged();
    }
}

QVariantList BookManager::getHighlightsForBook(int bookId) const
{
    QVariantList result;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, book_id, chapter, locator, text, color, created_at FROM highlights WHERE book_id=:b ORDER BY id DESC");
    q.bindValue(":b", bookId);
    if (q.exec()) {
        while (q.next()) {
            QVariantMap map;
            map["id"]        = q.value(0).toInt();
            map["bookId"]    = q.value(1).toInt();
            map["chapter"]   = q.value(2).toString();
            map["locator"]   = q.value(3).toString();
            map["text"]      = q.value(4).toString();
            map["color"]     = q.value(5).toString();
            map["createdAt"] = formatToLocalTime(q.value(6).toString());
            result.append(map);
        }
    }
    return result;
}

QVariantList BookManager::getAllHighlights() const
{
    QVariantList result;
    QSqlQuery q(m_db);
    q.exec(R"(
        SELECT h.id, h.book_id, b.title, b.author, b.format, h.chapter, h.locator, h.text, h.color, h.created_at
        FROM highlights h
        JOIN books b ON h.book_id = b.id
        ORDER BY h.id DESC
    )");

    while (q.next()) {
        QVariantMap map;
        map["id"]        = q.value(0).toInt();
        map["bookId"]    = q.value(1).toInt();
        map["bookTitle"] = q.value(2).toString();
        map["author"]    = q.value(3).toString();
        map["format"]    = q.value(4).toString();
        map["chapter"]   = q.value(5).toString();
        map["locator"]   = q.value(6).toString();
        map["text"]      = q.value(7).toString();
        map["color"]     = q.value(8).toString();
        map["createdAt"] = formatToLocalTime(q.value(9).toString());
        result.append(map);
    }
    return result;
}