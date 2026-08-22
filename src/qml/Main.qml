import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Window
import Raksha_Hub

ApplicationWindow {
    id: appWindow
    width: defaultWidth
    height: defaultHeight
    visible: true
    title: "Raksha Hub"
    color: "#121212"

    readonly property int defaultWidth:  1100
    readonly property int defaultHeight: 720

    function resetToDefaultWindow() {
        if (visibility === Window.FullScreen || visibility === Window.Maximized) {
            showNormal()
        }
        width = defaultWidth
        height = defaultHeight
    }

    // Global shortcut to resize the window  to default size
    Shortcut {
        sequence: "O"
        enabled: !hlSearchInput.activeFocus && !playerOpen && !bookReaderOpen
        onActivated: appWindow.resetToDefaultWindow()
    }


    Item {
        id: focusDummy
        focus: true
    }

    property string currentSection:          "Library"
    property int    openedShowId:            -1
    property string openedShowTitle:         ""
    property real   openedShowRating:        0.0
    property string openedPosterUrl:         ""
    property bool   showPageOpen:            false
    property bool   bookReaderOpen:         false
    property int    openedBookId:           -1
    property string openedBookTitle:        ""
    property string openedBookFormat:       ""
    property string openedBookUrl:          ""
    property string pendingHighlightLocator: ""
    property bool   globalHighlightsOpen:    false
    property string hlSearchText:            ""

    // Delete Confirmation State
    property bool   deleteConfirmOpen:  false
    property int    deleteTargetIndex: -1
    property string deleteTargetTitle: ""
    property string deleteTargetKind:  ""
    property string deleteTargetType:  "library"

    function requestDeleteLibraryItem(index, title, kind) {
        deleteTargetIndex = index
        deleteTargetTitle = title
        deleteTargetKind  = kind || "Item"
        deleteTargetType  = "library"
        deleteConfirmOpen = true
    }

    function requestDeleteBookItem(index, title) {
        deleteTargetIndex = index
        deleteTargetTitle = title
        deleteTargetKind  = "Book"
        deleteTargetType  = "book"
        deleteConfirmOpen = true
    }

    function executePendingDelete() {
        if (deleteTargetIndex < 0) return
        if (deleteTargetType === "library") {
            LibraryManager.removeAt(deleteTargetIndex)
        } else if (deleteTargetType === "book") {
            BookManager.removeAt(deleteTargetIndex)
        }
        deleteConfirmOpen = false
        deleteTargetIndex = -1
    }

    function getBookmarkSvg(filled, colorHex) {
        var col = encodeURIComponent(colorHex)
        if (filled) {
            return "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'><path fill='" + col + "' d='M24 2.5c-6.488 0-10.516.577-12.909 1.14c-2.505.591-3.918 2.759-4.088 5.061C6.779 11.715 6.5 17.236 6.5 26c0 6.958.102 11.522.232 14.516c.085 1.937.914 3.613 2.535 4.365c1.584.734 3.412.343 4.993-.695l7.271-4.772a4.5 4.5 0 0 1 4.938 0l7.27 4.772c1.582 1.038 3.41 1.429 4.994.695c1.62-.752 2.45-2.428 2.535-4.365c.13-2.994.232-7.558.232-14.516c0-8.764-.28-14.285-.503-17.299c-.17-2.302-1.583-4.47-4.088-5.06C34.516 3.077 30.488 2.5 24 2.5'/></svg>"
        } else {
            return "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'><path fill='none' stroke='" + col + "' stroke-linecap='round' stroke-linejoin='round' stroke-width='3.5' d='M8.499 8.812c.132-1.787 1.193-3.3 2.936-3.711C13.688 4.569 17.598 4 24 4s10.312.57 12.565 1.1c1.743.412 2.804 1.925 2.936 3.712c.22 2.971.499 8.455.499 17.188c0 6.946-.102 11.486-.231 14.451c-.137 3.147-2.573 4.21-5.206 2.48l-7.271-4.77a6 6 0 0 0-6.584 0l-7.27 4.77c-2.634 1.73-5.07.667-5.207-2.48C8.101 37.486 8 32.946 8 26c0-8.733.278-14.217.499-17.188'/></svg>"
        }
    }

    function getSearchSvg(colorHex) {
        var col = encodeURIComponent(colorHex)
        return "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='none' stroke='" + col + "' stroke-linecap='round' stroke-width='2' d='m15.879 15.879l4.242 4.242M18 10.5a7.5 7.5 0 1 1-15 0a7.5 7.5 0 0 1 15 0Z'/></svg>"
    }

    function getFilteredHighlights() {
        var all = BookManager.getAllHighlights()
        if (!hlSearchText || hlSearchText.trim() === "")
            return all
        var q = hlSearchText.trim().toLowerCase()
        return all.filter(function(item) {
            var txt = (item.text || "").toLowerCase()
            var bTitle = (item.bookTitle || "").toLowerCase()
            var author = (item.author || "").toLowerCase()
            return txt.indexOf(q) !== -1 || bTitle.indexOf(q) !== -1 || author.indexOf(q) !== -1
        })
    }

    onCurrentSectionChanged: {
        showPageOpen = false
        globalHighlightsOpen = false
        deleteConfirmOpen = false
        hlSearchText = ""
        if (typeof hlSearchInput !== "undefined" && hlSearchInput)
            hlSearchInput.text = ""
        focusDummy.forceActiveFocus()
        gc()
    }

    onBookReaderOpenChanged: {
        if (!bookReaderOpen) gc()
    }

    onPlayerOpenChanged: {
        if (!playerOpen) gc()
        else playerWindow.forceActiveFocus()
    }

    onGlobalHighlightsOpenChanged: {
        hlSearchText = ""
        if (typeof hlSearchInput !== "undefined" && hlSearchInput)
            hlSearchInput.text = ""
        focusDummy.forceActiveFocus()
    }

    function requestAddMovie() {
        movieFileDialog.open()
    }

    function requestAddShow() {
        showFolderDialog.open()
    }

    // Player state
    property bool   playerOpen:          false
    property string playerFilePath:      ""
    property real   playerStartMs:       0
    property int    playerEntryId:       -1
    property int    playerSeason:        0
    property int    playerEpisode:       0
    property string playerShowTitle:     ""
    property string playerEpisodeTitle:   ""

    function openPlayer(filePath, startMs, entryId, season, episode,
                        showTitleStr, epTitleStr) {
        if (season > 0 && entryId >= 0)
            LibraryManager.saveSetting("last_ep_" + entryId,
                                       season + "," + episode)
        playerFilePath            = filePath
        playerStartMs             = startMs
        playerEntryId             = entryId
        playerSeason              = season
        playerEpisode             = episode
        playerWindow.showTitle    = showTitleStr || ""
        playerWindow.episodeTitle = epTitleStr   || ""
        playerOpen                = true
    }

    function saveProgress(entryId, season, episode, positionMs, durationMs) {
        if (entryId < 0) return
        if (season === 0 && episode === 0)
            LibraryManager.updateMovieProgress(entryId, positionMs, durationMs)
        else
            LibraryManager.updateEpisodeProgress(entryId, season, episode,
                                                  positionMs, durationMs)
    }

    // Header
    header: ToolBar {
        height: 64
        visible: !playerOpen && !bookReaderOpen
        background: Rectangle {
            color: "#181818"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1; color: "#2a2a2a"
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20; anchors.rightMargin: 20
            spacing: 14

            MenuButton {
                Layout.alignment: Qt.AlignVCenter
                onClicked: {
                    if (showPageOpen) {
                        showPageOpen = false
                    } else if (currentSection === "Books" && globalHighlightsOpen) {
                        globalHighlightsOpen = false
                    } else {
                        drawer.open()
                    }
                }
            }

            Rectangle { width: 4; height: 22; radius: 2; color: "#4fc3f7" }

            Text {
                text: showPageOpen
                      ? openedShowTitle
                      : (currentSection === "Library" ? "My Library" : (currentSection === "Books" && globalHighlightsOpen ? "Highlights" : currentSection))
                color: "white"
                font.family: "Segoe UI, Inter, Roboto, Helvetica Neue, sans-serif"; font.bold: true
                font.pixelSize: 22; font.letterSpacing: 0.3
                Layout.fillWidth: true
            }

            // Highlights Search Bar
            Rectangle {
                visible: !showPageOpen && currentSection === "Books" && globalHighlightsOpen
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: hlSearchInput.activeFocus ? 220 : 160
                Layout.preferredHeight: 36
                radius: 18
                color: "#111111"
                border.color: hlSearchInput.activeFocus ? "#4fc3f7" : "#2a2a2a"
                border.width: 1

                Behavior on Layout.preferredWidth { NumberAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 6

                    Image {
                        width: 16; height: 16
                        source: appWindow.getSearchSvg(hlSearchInput.activeFocus ? "#4fc3f7" : "#666")
                        fillMode: Image.PreserveAspectFit
                    }

                    TextField {
                        id: hlSearchInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#e0e0e0"
                        font.family: "Segoe UI, Inter, Roboto, sans-serif"
                        font.pixelSize: 13
                        placeholderText: "search"
                        placeholderTextColor: "#555"
                        background: Rectangle { color: "transparent" }

                        onTextChanged: {
                            appWindow.hlSearchText = text
                        }

                        onEditingFinished: focusDummy.forceActiveFocus()

                        Keys.onReturnPressed: focusDummy.forceActiveFocus()

                        Keys.onEscapePressed: {
                            text = ""
                            appWindow.hlSearchText = ""
                            focusDummy.forceActiveFocus()
                        }
                    }

                    Text {
                        visible: hlSearchInput.text.length > 0
                        text: "✕"
                        color: "#666"
                        font.pixelSize: 11
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                hlSearchInput.text = ""
                                appWindow.hlSearchText = ""
                                focusDummy.forceActiveFocus()
                            }
                        }
                    }
                }
            }

            // all Highlights Bookmark Toggle Button
            Rectangle {
                visible: !showPageOpen && currentSection === "Books"
                Layout.alignment: Qt.AlignVCenter
                width: 36; height: 36; radius: 18
                color: globalHighlightsOpen ? "#1e2a3a" : (hlHeaderMa.containsMouse ? "#2a2a2a" : "#1e1e1e")
                border.color: globalHighlightsOpen ? "#4fc3f7" : "#333"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 120 } }

                Image {
                    anchors.centerIn: parent
                    width: 18; height: 18
                    source: appWindow.getBookmarkSvg(appWindow.globalHighlightsOpen, appWindow.globalHighlightsOpen ? "#4fc3f7" : "#aaa")
                    fillMode: Image.PreserveAspectFit
                }

                MouseArea {
                    id: hlHeaderMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        focusDummy.forceActiveFocus()
                        globalHighlightsOpen = !globalHighlightsOpen
                    }
                }
            }

            // Add Button in Header Top
            AddButton {
                visible: !showPageOpen && (currentSection === "Library" || currentSection === "Books")
                Layout.alignment: Qt.AlignVCenter
                onClicked: {
                    if (currentSection === "Books")
                        bookFileDialog.open()
                    else
                        addMenu.open = !addMenu.open
                }
            }
        }
    }

    // LAZY SECTION LOADER
    Loader {
        id: mainSectionLoader
        anchors.fill: parent
        active: !playerOpen && !bookReaderOpen

        sourceComponent: {
            if (showPageOpen && currentSection === "Library")
                return showPageComponent;
            if (currentSection === "Library")
                return libraryComponent;
            if (currentSection === "Books")
                return booksComponent;
            if (currentSection === "Music")
                return musicComponent;
            if (currentSection === "Games")
                return gamesComponent;
            return null;
        }
    }

    // Library Grid
    Component {
        id: libraryComponent
        GridView {
            id: libraryGrid
            anchors.fill: parent
            anchors.margins: 24
            cellHeight: 310; cellWidth: 205
            clip: true
            model: LibraryManager

            property int draggedIndex: -1
            property int hoverIndex:   -1

            populate: Transition {
                NumberAnimation { properties: "x,y"; duration: 600; easing.type: Easing.OutCubic }
            }
            add: Transition {
                NumberAnimation { properties: "x,y"; duration: 600; easing.type: Easing.OutCubic }
            }

            delegate: ShowCard {
                showTitle:     model.title
                showKind:      model.kind
                showRating:    model.rating
                showProgress:  model.progress
                showPosterUrl: model.posterUrl
                showDuration:  model.duration
                entryId:       model.entryId

                onDeleteRequested: appWindow.requestDeleteLibraryItem(index, model.title, model.kind)
                onMoveRequested: function(from, to) { LibraryManager.moveEntry(from, to) }

                onShowClicked: {
                    if (model.kind === "Show") {
                        openedShowId     = model.entryId
                        openedShowTitle  = model.title
                        openedShowRating = model.rating
                        openedPosterUrl  = model.posterUrl
                        showPageOpen     = true
                    } else {
                        appWindow.openPlayer(
                            model.folderPath, model.positionMs,
                            model.entryId, 0, 0, model.title, "")
                    }
                }
            }
        }
    }

    // Show Page
    Component {
        id: showPageComponent
        ShowPage {
            entryId:    openedShowId
            showTitle:  openedShowTitle
            showRating: openedShowRating
            posterUrl:  openedPosterUrl
            onBackRequested: showPageOpen = false
            onPlayRequested: function(filePath, startMs, entryId, season, episode, epTitle) {
                appWindow.openPlayer(filePath, startMs, entryId, season, episode, openedShowTitle, epTitle)
            }
        }
    }

    // Books Section
    Component {
        id: booksComponent
        Item {
            anchors.fill: parent

            Component.onCompleted: BookManager.ensureCovers()

            Connections {
                target: BookManager
                function onHighlightsChanged() {
                    if (globalHlList.visible)
                        globalHlList.model = appWindow.getFilteredHighlights()
                }
            }

            //  Books Grid View
            GridView {
                id: booksGrid
                anchors.fill: parent
                anchors.margins: 24
                cellHeight: 310; cellWidth: 205
                clip: true
                visible: !appWindow.globalHighlightsOpen
                model: BookManager

                delegate: BookCard {
                    bookId:       model.bookId
                    bookTitle:    model.title
                    bookAuthor:   model.author
                    bookFormat:   model.format
                    coverPath:    model.coverPath
                    bookProgress: model.progress

                    onDeleteRequested: appWindow.requestDeleteBookItem(index, model.title)
                    onBookClicked: {
                        openedBookId     = model.bookId
                        openedBookTitle  = model.title
                        openedBookFormat = model.format
                        openedBookUrl    = BookManager.readerUrl(model.bookId)
                        pendingHighlightLocator = ""
                        bookReaderOpen   = true
                    }
                }
            }

            // all Highlights
            ListView {
                id: globalHlList
                objectName: "globalHlList"
                anchors.fill: parent
                anchors.margins: 24
                clip: true
                spacing: 12
                visible: appWindow.globalHighlightsOpen
                model: appWindow.getFilteredHighlights()

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: focusDummy.forceActiveFocus()
                }

                delegate: Rectangle {
                    id: hlCard
                    width: ListView.view.width
                    implicitHeight: Math.max(76, cardLayout.implicitHeight + 24)
                    height: implicitHeight
                    radius: 10
                    color: "#181818"
                    border.color: "#2a2a2a"
                    border.width: 1

                    RowLayout {
                        id: cardLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            width: 6
                            implicitHeight: 48
                            Layout.fillHeight: true
                            radius: 3
                            color: modelData.color || "#ffe082"
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: modelData.bookTitle || "Unknown Book"
                                    color: "#4fc3f7"
                                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                    font.bold: true
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 240
                                }
                                Text {
                                    visible: modelData.author && modelData.author.length > 0
                                    text: "by " + modelData.author
                                    color: "#aaa"
                                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "[" + (modelData.format || "EPUB") + "]"
                                    color: "#666"
                                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                    font.pixelSize: 11
                                }
                            }

                            TextEdit {
                                text: modelData.text || ""
                                color: "#e0e0e0"
                                font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                font.pixelSize: 14
                                readOnly: true
                                selectByMouse: true
                                activeFocusOnPress: false
                                wrapMode: TextEdit.Wrap
                                Layout.fillWidth: true
                            }
                        }

                        // Page / Chapter, Date, Time
                        ColumnLayout {
                            spacing: 2
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            Layout.preferredWidth: 140

                            // Page or Chapter
                            Text {
                                text: {
                                    if (modelData.chapter && modelData.chapter !== "")
                                        return modelData.chapter;
                                    if (modelData.locator && modelData.locator.indexOf("pdfpage:") === 0) {
                                        var p = modelData.locator.replace("pdfpage:", "").split("_")[0];
                                        return "Page " + p;
                                    }
                                    return "";
                                }
                                visible: text !== ""
                                color: "#4fc3f7"
                                font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                font.bold: true
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignRight
                                elide: Text.ElideRight
                                Layout.maximumWidth: 140
                            }

                            // Date
                            Text {
                                text: {
                                    var str = modelData.createdAt || ""
                                    if (str.length >= 10) return str.substring(0, 10)
                                    return ""
                                }
                                color: "#888888"
                                font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                font.pixelSize: 11
                                Layout.alignment: Qt.AlignRight
                            }

                            // Time
                            Text {
                                text: {
                                    var str = modelData.createdAt || ""
                                    if (str.length >= 16) return str.substring(11, 16)
                                    return ""
                                }
                                color: "#666666"
                                font.family: "Segoe UI, Inter, Roboto, sans-serif"
                                font.pixelSize: 11
                                Layout.alignment: Qt.AlignRight
                            }
                        }

                        // Delete Button
                        Rectangle {
                            z: 10
                            width: 30
                            height: 30
                            radius: 15
                            color: delGMa.containsMouse ? "#3a1a1a" : "#2a1a1a"
                            border.color: delGMa.containsMouse ? "#ff5252" : "#333333"
                            border.width: 1
                            Layout.alignment: Qt.AlignVCenter

                            Text {
                                text: "✕"
                                color: delGMa.containsMouse ? "#ff5252" : "#888"
                                anchors.centerIn: parent
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: delGMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    mouse.accepted = true
                                    focusDummy.forceActiveFocus()
                                    if (modelData.id > 0)
                                        BookManager.removeHighlight(modelData.id)
                                    else if (modelData.locator)
                                        BookManager.removeHighlightByLocator(modelData.locator)

                                    globalHlList.model = appWindow.getFilteredHighlights()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Music section
    Component {
        id: musicComponent
        Item { anchors.fill: parent }
    }

    // Games section
    Component {
        id: gamesComponent
        Item { anchors.fill: parent }
    }

    // PLAYER OVERLAY
    PlayerWindow {
        id: playerWindow
        anchors.fill: parent
        z: 999
        visible: playerOpen
        focus: playerOpen

        filePath:     appWindow.playerFilePath
        startMs:      appWindow.playerStartMs
        entryId:      appWindow.playerEntryId
        season:       appWindow.playerSeason
        episode:      appWindow.playerEpisode
        showTitle:    appWindow.playerShowTitle
        episodeTitle: appWindow.playerEpisodeTitle

        onPlaybackStopped: function(entryId, season, episode, positionMs, durationMs) {
            appWindow.saveProgress(entryId, season, episode, positionMs, durationMs)
        }
    }

    // BOOK READER OVERLAY
    Rectangle {
        anchors.fill: parent
        z: 997
        color: "#111111"
        visible: bookReaderOpen
    }

    Loader {
        anchors.fill: parent
        z: 998
        active: bookReaderOpen
        sourceComponent: Component {
            BookReaderPage {
                bookId:     openedBookId
                bookTitle:  openedBookTitle
                bookFormat: openedBookFormat
                readerUrl:  openedBookUrl
                onCloseRequested: bookReaderOpen = false
            }
        }
    }

    // SIDE MENU
    Drawer {
        id: drawer
        edge: Qt.LeftEdge; width: 260; height: appWindow.height
        background: Rectangle { color: "#1c1c1c" }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 32
            spacing: 4

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 20
                text: "Menu"
                color: "#4fc3f7"
                font.family: "Segoe UI, Inter, Roboto, sans-serif"
                font.bold: true
                font.pixelSize: 22
                font.letterSpacing: 1.2
            }

            Repeater {
                model: ["Library", "Books", "Music", "Games"]
                delegate: SideMenuItem {
                    label: modelData
                    active: currentSection === modelData
                    onClicked: {
                        focusDummy.forceActiveFocus()
                        currentSection = modelData
                        showPageOpen   = false
                        drawer.close()
                    }
                }
            }

            Item { Layout.fillHeight: true }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 20
                spacing: 4

                Text {
                    text: "made with"
                    color: "#444"
                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                    font.italic: true
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: "🌿"
                    font.pixelSize: 11
                    opacity: 0.35
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: "by RiRiRonRon"
                    color: "#444"
                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                    font.italic: true
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // ADD MENU
    MouseArea {
        anchors.fill: parent
        visible: addMenu.open; enabled: addMenu.open
        onClicked: addMenu.open = false
    }

    AddMenu {
        id: addMenu
        anchors.top: parent.top; anchors.right: parent.right
        anchors.topMargin: 8; anchors.rightMargin: 20
        onMovieSelected: { open = false; appWindow.requestAddMovie() }
        onShowSelected:  { open = false; appWindow.requestAddShow() }
    }

    FileDialog {
        id: bookFileDialog
        title: "Select a book"
        nameFilters: ["Books (*.pdf *.epub *.cbz)"]
        parentWindow: appWindow
        onAccepted: BookManager.addBook(selectedFile)
    }

    FileDialog {
        id: movieFileDialog
        title: "Select a movie file"
        nameFilters: ["Video files (*.mp4 *.mkv *.avi *.mov)"]
        parentWindow: appWindow
        onAccepted: LibraryManager.addMovie(selectedFile)
    }

    FolderDialog {
        id: showFolderDialog
        title: "Select the show's root folder"
        parentWindow: appWindow
        onAccepted: LibraryManager.addShow(selectedFolder)
    }

    Connections {
        target: LibraryManager
        function onMovieAdded(title)     { toast.show("Added " + title) }
        function onShowAdded(title)      { toast.show("Added " + title) }
        function onItemRemoved(title)    { toast.show(title + " deleted") }
        function onDuplicateMovie(title) { toast.show(title + " already exists") }
        function onDuplicateShow(title)  { toast.show(title + " already exists") }
    }

    Toast {
        id: toast
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24
    }

    // DELETE CONFIRMATION
    Rectangle {
        id: deleteConfirmOverlay
        anchors.fill: parent
        z: 9990
        color: "#d0000000"
        visible: appWindow.deleteConfirmOpen

        Behavior on opacity { NumberAnimation { duration: 120 } }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                focusDummy.forceActiveFocus()
                appWindow.deleteConfirmOpen = false
            }
        }

        Item {
            focus: appWindow.deleteConfirmOpen
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    focusDummy.forceActiveFocus()
                    appWindow.deleteConfirmOpen = false
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    focusDummy.forceActiveFocus()
                    appWindow.executePendingDelete()
                    event.accepted = true
                }
            }
        }

        Rectangle {
            id: deleteModalBox
            width: 270
            height: 125
            radius: 10
            color: "#1c1c1c"
            border.color: "#2a2a2a"
            border.width: 1
            anchors.centerIn: parent

            MouseArea {
                anchors.fill: parent
                onClicked: focusDummy.forceActiveFocus()
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                Text {
                    text: "Confirm delete"
                    color: "#f0f0f0"
                    font.family: "Segoe UI, Inter, Roboto, sans-serif"
                    font.bold: true
                    font.pixelSize: 16
                    Layout.alignment: Qt.AlignHCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12


                    Rectangle {
                        width: 90
                        height: 32
                        radius: 6
                        color: noDelMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                        border.color: "#333333"
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 100 } }

                        Text {
                            text: "No"
                            color: "#aaaaaa"
                            font.family: "Segoe UI, Inter, Roboto, sans-serif"
                            font.pixelSize: 13
                            font.bold: true
                            anchors.centerIn: parent
                        }

                        MouseArea {
                            id: noDelMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                focusDummy.forceActiveFocus()
                                appWindow.deleteConfirmOpen = false
                            }
                        }
                    }


                    Rectangle {
                        width: 90
                        height: 32
                        radius: 6
                        color: yesDelMa.containsMouse ? "#4a1a1a" : "#2a1a1a"
                        border.color: "#ff5252"
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: 100 } }

                        Text {
                            text: "Yes"
                            color: "#ff5252"
                            font.family: "Segoe UI, Inter, Roboto, sans-serif"
                            font.pixelSize: 13
                            font.bold: true
                            anchors.centerIn: parent
                        }

                        MouseArea {
                            id: yesDelMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                focusDummy.forceActiveFocus()
                                appWindow.executePendingDelete()
                            }
                        }
                    }
                }
            }
        }
    }
}