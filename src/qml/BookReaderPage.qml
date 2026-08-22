import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Window
import Raksha_Hub

Item {
    id: readerPage

    property int    bookId:     -1
    property string bookTitle:  ""
    property string bookFormat: ""
    property string readerUrl:  ""

    property string currentTheme:      "sepia"
    property int    currentFontSize:   140
    property string currentFlow:       "scrolled"
    property int    currentPadding:    -4
    property int    currentLineSpacing: 220
    property string currentTextColor:  ""
    property int    currentBrightness: 100
    property string currentFontFamily: "Verdana"
    property string fontSearchText:    ""

    property int    currentPage:  0
    property int    totalPages:   0
    property real   progressPct:  0.0

    property int    searchTotal:   0
    property int    searchCurrent: 0

    property bool   colorPickerOverlayVisible: false
    property bool   isClosing:                 false
    property bool   pageInputActive:           false

    readonly property int minPadding: -120
    readonly property int maxPadding:  160

    readonly property int minLineSpacing: 100
    readonly property int maxLineSpacing: 300

    signal closeRequested()

    Component.onDestruction: saveCurrentPdfProgress()
    onVisibleChanged: if (!visible) saveCurrentPdfProgress()

    function saveCurrentPdfProgress() {
        if (readerPage.bookFormat === "PDF" && readerPage.bookId >= 0 && webView.ready) {
            webView.executeScript("if(window.savePdfToHost) window.savePdfToHost();")
            webView.executeScript(
                "(function(){" +
                "  var app=window.PDFViewerApplication;" +
                "  if(app&&app.pdfDocument&&window.chrome&&window.chrome.webview){" +
                "    var pg=app.page||1, tot=app.pagesCount||1;" +
                "    window.chrome.webview.postMessage(JSON.stringify({" +
                "      type:'progress',page:pg,totalPages:tot," +
                "      pct:tot>1?(pg-1)/(tot-1):0,cfi:'pdfpage:'+pg" +
                "    }));" +
                "  }" +
                "})()"
            )
        }
    }

    Timer {
        id: closeTimer
        interval: 300
        repeat: false
        onTriggered: {
            readerPage.isClosing = false
            readerPage.closeRequested()
        }
    }

    function jumpToPage(targetPage) {
        var p = parseInt(targetPage, 10)
        if (isNaN(p) || p < 1) p = 1
        if (totalPages > 0 && p > totalPages) p = totalPages

        if (bookFormat === "PDF") {
            webView.executeScript("if(window.goToPdfPage) window.goToPdfPage(" + p + ")")
        } else {
            var tot = totalPages > 1 ? totalPages : 100
            var pct = tot > 1 ? (p - 1) / (tot - 1) : 0
            webView.executeScript("if(window.goToLocation) window.goToLocation('scrollpct:' + " + pct.toFixed(6) + ")")
        }

        currentPage = p
        pageInputActive = false
        readerPage.reclaimInputFocus(null)
    }

    function clampPadding(v) {
        return Math.max(minPadding, Math.min(maxPadding, v))
    }

    function clampLineSpacing(v) {
        return Math.max(minLineSpacing, Math.min(maxLineSpacing, v))
    }

    function saveAllSettings() {
        if (bookId < 0)
            return

        BookManager.saveReaderSettings(
            bookId,
            currentTheme,
            currentFontSize,
            currentFlow,
            currentPadding,
            currentTextColor,
            currentBrightness,
            currentFontFamily,
            currentLineSpacing
        )
    }

    function reclaimInputFocus(targetField) {
        if (Window.window)
            Window.window.requestActivate()

        if (targetField)
            targetField.forceActiveFocus(Qt.MouseFocusReason)
        else
            readerPage.forceActiveFocus(Qt.OtherFocusReason)
    }

    function restoreHighlights() {
        if (!webView.ready || bookId < 0)
            return

        if (readerPage.bookFormat === "EPUB") {
            var list = BookManager.getHighlightsForBook(bookId)
            webView.executeScript("if(window.loadHighlights)window.loadHighlights(" + JSON.stringify(list) + ")")
        }

        if (typeof appWindow !== "undefined" && appWindow && appWindow.pendingHighlightLocator && appWindow.pendingHighlightLocator !== "") {
            if (readerPage.bookFormat === "PDF") {
                var loc = appWindow.pendingHighlightLocator
                if (loc && loc.indexOf("pdfpage:") === 0) {
                    var pNum = parseInt(loc.replace("pdfpage:", ""), 10)
                    if (pNum > 0) {
                        webView.executeScript("if(window.goToPdfPage)window.goToPdfPage(" + pNum + ")")
                    }
                }
            } else {
                webView.executeScript("if(window.goToHighlight)window.goToHighlight(" + JSON.stringify(appWindow.pendingHighlightLocator) + ")")
            }
            appWindow.pendingHighlightLocator = ""
        }
    }

    onBookIdChanged: {
        if (bookId >= 0) {
            var s = BookManager.readerSettings(bookId)

            currentTheme = (s.theme !== undefined && s.theme !== "") ? s.theme : "sepia"
            currentFontSize = (s.fontSize !== undefined) ? Math.max(60, Math.min(200, s.fontSize)) : 140
            currentFlow = (s.flow !== undefined && s.flow !== "") ? s.flow : "scrolled"
            currentPadding = (s.padding !== undefined) ? clampPadding(s.padding) : -4
            currentTextColor = (s.textColor !== undefined) ? s.textColor : ""
            currentBrightness = (s.brightness !== undefined) ? Math.max(30, Math.min(200, s.brightness)) : 100
            currentFontFamily = (s.fontFamily !== undefined && s.fontFamily !== "") ? s.fontFamily : "Verdana"
            currentLineSpacing = (s.lineSpacing !== undefined) ? clampLineSpacing(s.lineSpacing) : 220
        }
    }

    Connections {
        target: BookManager

        function onHighlightsChanged() {
            restoreHighlights()
        }

        function onPdfSaved(savedId) {
            if (readerPage.isClosing && savedId === readerPage.bookId) {
                closeTimer.stop()
                readerPage.isClosing = false
                readerPage.closeRequested()
            }
        }
    }

    WebViewItem {
        id: webView

        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.top:    parent.top
        anchors.bottom: settingsPanel.top

        visible: readerPage.visible

        Component.onCompleted: loadContent()

        onReadyChanged: {
            if (ready) {
                loadContent()
            }
        }

        function loadContent() {
            if (!ready)
                return

            var r = readerPage.readerUrl
            if (r.length > 0)
                url = r
        }

        onLoadFailed: function(reason) {
            console.log("Book load failed:", reason)
        }

        onWebMessageReceived: function(json) {
            try {
                var msg = JSON.parse(json)

                if (msg.type === "clickOutside") {
                    if (settingsPanel.open)
                        settingsPanel.open = false

                    readerPage.colorPickerOverlayVisible = false
                    readerPage.pageInputActive = false
                } else if (msg.type === "epubLoaded" || msg.type === "pdfLoaded") {
                    readerPage.restoreHighlights()

                    if (readerPage.bookFormat === "PDF" && readerPage.bookId >= 0) {
                        var loc = BookManager.lastLocation(readerPage.bookId)

                        if (loc && loc.indexOf("pdfpage:") === 0) {
                            var pNum = parseInt(loc.replace("pdfpage:", ""), 10)

                            if (pNum > 0) {
                                webView.executeScript("if(window.goToPdfPage)window.goToPdfPage(" + pNum + ")")
                            }
                        }
                    }
                } else if (msg.type === "addHighlight") {
                    BookManager.addHighlight(
                        readerPage.bookId,
                        msg.chapter || "",
                        msg.locator || "",
                        msg.text || "",
                        msg.color || "#ffe082"
                    )
                } else if (msg.type === "removeHighlight") {
                    if (msg.id > 0)
                        BookManager.removeHighlight(msg.id)
                    else if (msg.locator && msg.locator !== "")
                        BookManager.removeHighlightByLocator(msg.locator)
                } else if (msg.type === "updateHighlightColor") {
                    if (msg.locator && msg.locator !== "")
                        BookManager.updateHighlightColor(msg.locator, msg.color || "#ffe082")
                } else if ((msg.type === "savePdfBinary" || msg.type === "pdfSaveFailed") && readerPage.bookId >= 0) {
                    if (msg.type === "savePdfBinary" && msg.data && msg.data.length > 0) {
                        BookManager.savePdfBinaryFile(readerPage.bookId, msg.data)
                    } else if (readerPage.isClosing) {
                        closeTimer.stop()
                        readerPage.isClosing = false
                        readerPage.closeRequested()
                    }
                } else if (msg.type === "progress" && readerPage.bookId >= 0) {
                    if (typeof msg.pct === "number")
                        readerPage.progressPct = msg.pct

                    if (typeof msg.page === "number")
                        readerPage.currentPage = msg.page

                    if (typeof msg.totalPages === "number")
                        readerPage.totalPages = msg.totalPages

                    BookManager.updateProgress(
                        readerPage.bookId,
                        readerPage.currentPage,
                        readerPage.totalPages
                    )

                    if (msg.cfi)
                        BookManager.saveLocation(readerPage.bookId, msg.cfi)
                } else if (msg.type === "searchResult") {
                    readerPage.searchTotal   = msg.total   || 0
                    readerPage.searchCurrent = msg.current || 0
                }
            } catch(e) {}
        }
    }

    Loader {
        id: colorPickerLoader
        anchors.fill: parent
        z: 500
        active: readerPage.colorPickerOverlayVisible

        sourceComponent: Component {
            Rectangle {
                id: colorPickerOverlay
                anchors.fill: parent
                color: "#aa000000"

                Behavior on color { ColorAnimation { duration: 200 } }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        readerPage.colorPickerOverlayVisible = false
                        readerPage.reclaimInputFocus(null)
                    }
                }

                Rectangle {
                    id: colorPickerModal
                    anchors.centerIn: parent
                    width: 320
                    height: 410
                    radius: 14
                    color: "#1e1e1e"
                    border.color: "#333"
                    border.width: 1

                    property real hue:        0.0
                    property real saturation: 1.0
                    property real lightness:  0.5

                    readonly property string pickedColor: Qt.hsla(hue, saturation, lightness, 1.0)

                    onHueChanged:        if (!hexField.activeFocus) syncHex()
                    onSaturationChanged: if (!hexField.activeFocus) syncHex()
                    onLightnessChanged:  if (!hexField.activeFocus) syncHex()

                    function syncHex() {
                        if (hexField) {
                            hexField.text = pickedColor.toString()
                                .replace(/^#/, "")
                                .slice(0, 6)
                                .toLowerCase()
                        }
                    }

                    function applyHex(hex) {
                        hex = hex.replace(/^#/, "")

                        if (hex.length === 3)
                            hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2]

                        if (hex.length !== 6)
                            return false

                        if (!/^[0-9a-fA-F]{6}$/.test(hex))
                            return false

                        var r = parseInt(hex.substring(0, 2), 16) / 255
                        var g = parseInt(hex.substring(2, 4), 16) / 255
                        var b = parseInt(hex.substring(4, 6), 16) / 255

                        var mx = Math.max(r, g, b)
                        var mn = Math.min(r, g, b)

                        var h = 0
                        var s = 0
                        var l = (mx + mn) / 2

                        if (mx !== mn) {
                            var d = mx - mn
                            s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn)

                            if (mx === r)
                                h = ((g - b) / d + (g < b ? 6 : 0)) / 6
                            else if (mx === g)
                                h = ((b - r) / d + 2) / 6
                            else
                                h = ((r - g) / d + 4) / 6
                        }

                        hue = h
                        saturation = s
                        lightness = l

                        return true
                    }

                    MouseArea { anchors.fill: parent; onClicked: {} }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                text: "\uD83C\uDFA8"
                                font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "Text Color"
                                color: "#e0e0e0"
                                font.family: "Consolas"
                                font.pixelSize: 14
                                font.bold: true
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: 4

                            Text {
                                text: "Hue"
                                color: "#666"
                                font.family: "Consolas"
                                font.pixelSize: 10
                            }

                            Rectangle {
                                width: parent.width
                                height: 16
                                radius: 8
                                clip: true

                                gradient: Gradient {
                                    orientation: Gradient.Horizontal

                                    GradientStop { position: 0.000; color: "#ff0000" }
                                    GradientStop { position: 0.167; color: "#ffff00" }
                                    GradientStop { position: 0.333; color: "#00ff00" }
                                    GradientStop { position: 0.500; color: "#00ffff" }
                                    GradientStop { position: 0.667; color: "#0000ff" }
                                    GradientStop { position: 0.833; color: "#ff00ff" }
                                    GradientStop { position: 1.000; color: "#ff0000" }
                                }

                                Rectangle {
                                    x: colorPickerModal.hue * (parent.width - 16)
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 16
                                    height: 20
                                    radius: 4
                                    color: Qt.hsla(colorPickerModal.hue, 1.0, 0.5, 1)
                                    border.color: "white"
                                    border.width: 2
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor

                                    function update(m) {
                                        colorPickerModal.hue = Math.max(0, Math.min(1, m.x / width))
                                    }

                                    onPressed:         (m) => update(m)
                                    onPositionChanged: (m) => { if (pressed) update(m) }
                                }
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: 4

                            Text {
                                text: "Color"
                                color: "#666"
                                font.family: "Consolas"
                                font.pixelSize: 10
                            }

                            Rectangle {
                                width: parent.width
                                height: 120
                                radius: 8
                                clip: true

                                Rectangle {
                                    anchors.fill: parent
                                    color: Qt.hsla(colorPickerModal.hue, 1.0, 0.5, 1)
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: "#ffffffff" }
                                        GradientStop { position: 1.0; color: "#00ffffff" }
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#00000000" }
                                        GradientStop { position: 1.0; color: "#ff000000" }
                                    }
                                }

                                Rectangle {
                                    x: colorPickerModal.saturation * (parent.width - 14)
                                    y: (1 - (colorPickerModal.lightness - 0.05) / 0.80) * (parent.height - 14)
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: "transparent"
                                    border.color: "white"
                                    border.width: 2

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 3
                                        radius: 5
                                        color: colorPickerModal.pickedColor
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.CrossCursor

                                    function update(m) {
                                        colorPickerModal.saturation = Math.max(0, Math.min(1, m.x / width))
                                        colorPickerModal.lightness  = 0.85 - Math.max(0, Math.min(1, m.y / height)) * 0.80
                                    }

                                    onPressed:         (m) => update(m)
                                    onPositionChanged: (m) => { if (pressed) update(m) }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: [
                                    "#e0e0e0", "#ffffff", "#f5f0e8", "#aaaaaa", "#111111",
                                    "#4fc3f7", "#f5c518", "#e57373", "#81c784", "#ce93d8"
                                ]

                                delegate: Rectangle {
                                    width: (parent.width - 54) / 10
                                    height: 22
                                    radius: 11
                                    color: modelData

                                    border.color: colorPickerModal.pickedColor.toString().toLowerCase() === modelData
                                                  ? "#4fc3f7"
                                                  : "#444"

                                    border.width: colorPickerModal.pickedColor.toString().toLowerCase() === modelData
                                                  ? 2
                                                  : 1

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: colorPickerModal.applyHex(modelData)
                                    }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 10

                            Rectangle {
                                width: 44
                                height: 44
                                radius: 8
                                color: "#111"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Aa"
                                    color: colorPickerModal.pickedColor
                                    font.family: "Georgia"
                                    font.pixelSize: 15
                                    font.bold: true
                                }
                            }

                            Column {
                                spacing: 6
                                width: parent.width - 54

                                Rectangle {
                                    id: hexBox
                                    width: parent.width
                                    height: 30
                                    radius: 6
                                    color: "#111"

                                    border.color: hexField.activeFocus ? "#4fc3f7"
                                                  : hexField.text.length === 0 ? "#333"
                                                  : (hexField.text.length === 3 || hexField.text.length === 6)
                                                    && /^[0-9a-fA-F]+$/.test(hexField.text) ? "#2e7d32"
                                                  : "#c62828"

                                    border.width: hexField.activeFocus ? 2 : 1

                                    Behavior on border.color { ColorAnimation { duration: 120 } }

                                    Text {
                                        id: hashPrefix
                                        text: "#"
                                        color: "#666"
                                        font.family: "Consolas"
                                        font.pixelSize: 13
                                        font.bold: true
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    TextField {
                                        id: hexField
                                        anchors.left: hashPrefix.right
                                        anchors.leftMargin: 2
                                        anchors.right: setHexBtn.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: parent.height - 4

                                        color: "#e0e0e0"
                                        font.family: "Consolas"
                                        font.pixelSize: 13
                                        font.bold: true

                                        placeholderText: "rrggbb"
                                        placeholderTextColor: "#444"

                                        maximumLength: 6
                                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                                        selectByMouse: true
                                        activeFocusOnPress: true

                                        background: Rectangle { color: "transparent" }

                                        onPressed: readerPage.reclaimInputFocus(hexField)

                                        onTextChanged: {
                                            var clean = text.replace(/[^0-9a-fA-F]/g, "")
                                            if (clean !== text) {
                                                text = clean
                                                return
                                            }
                                        }

                                        Keys.onReturnPressed: {
                                            var clean = text.replace(/[^0-9a-fA-F]/g, "")
                                            if (clean.length === 3 || clean.length === 6) {
                                                colorPickerModal.applyHex(clean)
                                                colorPickerModal.syncHex()
                                            }
                                        }

                                        onEditingFinished: {
                                            var clean = text.replace(/[^0-9a-fA-F]/g, "")
                                            if (clean.length === 3 || clean.length === 6)
                                                colorPickerModal.applyHex(clean)
                                                colorPickerModal.syncHex()
                                        }
                                    }

                                    Rectangle {
                                        id: setHexBtn
                                        anchors.right: parent.right
                                        anchors.rightMargin: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 36
                                        height: 22
                                        radius: 5

                                        color: setHexMa.containsMouse ? "#2a5a3a" : "#1a3a2a"
                                        border.color: "#4caf50"
                                        border.width: 1

                                        visible: hexField.text.length === 3 || hexField.text.length === 6

                                        Behavior on color { ColorAnimation { duration: 80 } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: "Set"
                                            color: "#4fc3f7"
                                            font.family: "Consolas"
                                            font.pixelSize: 10
                                            font.bold: true
                                        }

                                        MouseArea {
                                            id: setHexMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor

                                            onClicked: {
                                                var clean = hexField.text.replace(/[^0-9a-fA-F]/g, "")
                                                if (clean.length === 3 || clean.length === 6) {
                                                    colorPickerModal.applyHex(clean)
                                                    colorPickerModal.syncHex()
                                                }
                                            }
                                        }
                                    }
                                }

                                Row {
                                    spacing: 8

                                    Rectangle {
                                        width: 70
                                        height: 28
                                        radius: 7
                                        color: cancelMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                        border.color: "#444"
                                        border.width: 1

                                        Behavior on color { ColorAnimation { duration: 80 } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: "Cancel"
                                            color: "#aaa"
                                            font.family: "Consolas"
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            id: cancelMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor

                                            onClicked: {
                                                readerPage.colorPickerOverlayVisible = false
                                                readerPage.reclaimInputFocus(null)
                                            }
                                        }
                                    }

                                    Rectangle {
                                        width: 70
                                        height: 28
                                        radius: 7
                                        color: applyMa.containsMouse ? "#2a2a2a" : "#1a3a5a"
                                        border.color: "#4fc3f7"
                                        border.width: 1

                                        Behavior on color { ColorAnimation { duration: 80 } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: "Apply"
                                            color: "#4fc3f7"
                                            font.family: "Consolas"
                                            font.pixelSize: 12
                                            font.bold: true
                                        }

                                        MouseArea {
                                            id: applyMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor

                                            onClicked: {
                                                readerPage.currentTextColor = colorPickerModal.pickedColor

                                                webView.executeScript(
                                                    "if(window.setEpubTextColor)window.setEpubTextColor("
                                                    + JSON.stringify(colorPickerModal.pickedColor)
                                                    + ")"
                                                )

                                                readerPage.saveAllSettings()
                                                readerPage.colorPickerOverlayVisible = false
                                                readerPage.reclaimInputFocus(null)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: settingsPanel

        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: controlsBar.top

        property bool open: false

        height: open ? 320 : 0
        clip: true
        z: 200

        color: "#161616"
        border.color: "#2a2a2a"
        border.width: 1

        Behavior on height {
            NumberAnimation { duration: 220; easing.type: Easing.OutQuad }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        onOpenChanged: {
            if (open) {
                Qt.callLater(function() {
                    readerPage.reclaimInputFocus(searchField)
                })
            } else {
                readerPage.reclaimInputFocus(null)
            }
        }

        Item {
            anchors.fill: parent
            anchors.margins: 14

            opacity: settingsPanel.open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }

            Item {
                id: searchRow
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 36

                Rectangle {
                    id: searchBox
                    anchors.left: parent.left
                    anchors.right: searchBtnRow.left
                    anchors.rightMargin: 8
                    height: 36
                    radius: 8
                    color: "#111"

                    border.color: searchField.activeFocus ? "#4fc3f7" : "#2a2a2a"
                    border.width: 1

                    Behavior on border.color { ColorAnimation { duration: 120 } }

                    TextField {
                        id: searchField
                        anchors.fill: parent
                        anchors.margins: 2

                        color: "#e0e0e0"
                        font.family: "Consolas"
                        font.pixelSize: 13

                        placeholderText: "Search in book..."
                        background: Rectangle { color: "transparent" }

                        onPressed: readerPage.reclaimInputFocus(searchField)

                        onTextChanged: {
                            if (text.length === 0) {
                                webView.executeScript("window.searchInBook&&window.searchInBook('')")
                                readerPage.searchTotal = 0
                            }
                        }

                        Keys.onReturnPressed: {
                            webView.executeScript(
                                "window.searchInBook&&window.searchInBook("
                                + JSON.stringify(text)
                                + ")"
                            )
                        }
                    }
                }

                Row {
                    id: searchBtnRow
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Rectangle {
                        width: 36
                        height: 36
                        radius: 8
                        color: sGoMa.containsMouse ? "#1e3a4a" : "#1a2a3a"
                        border.color: "#4fc3f7"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "⌕"
                            color: "#4fc3f7"
                            font.pixelSize: 16
                        }

                        MouseArea {
                            id: sGoMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            onClicked: {
                                webView.executeScript(
                                    "window.searchInBook&&window.searchInBook("
                                    + JSON.stringify(searchField.text)
                                    + ")"
                                )

                                readerPage.reclaimInputFocus(searchField)
                            }
                        }
                    }

                    Rectangle {
                        width: 36
                        height: 36
                        radius: 8
                        visible: readerPage.searchTotal > 0
                        color: sPMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                        border.color: "#333"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "↑"
                            color: "#aaa"
                            font.pixelSize: 14
                        }

                        MouseArea {
                            id: sPMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            onClicked: {
                                webView.executeScript("window.searchPrev&&window.searchPrev()")
                                readerPage.reclaimInputFocus(searchField)
                            }
                        }
                    }

                    Rectangle {
                        width: 36
                        height: 36
                        radius: 8
                        visible: readerPage.searchTotal > 0
                        color: sNMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                        border.color: "#333"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "↓"
                            color: "#aaa"
                            font.pixelSize: 14
                        }

                        MouseArea {
                            id: sNMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            onClicked: {
                                webView.executeScript("window.searchNext&&window.searchNext()")
                                readerPage.reclaimInputFocus(searchField)
                            }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: readerPage.searchTotal > 0
                        text: readerPage.searchCurrent + "/" + readerPage.searchTotal
                        color: "#4fc3f7"
                        font.family: "Consolas"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }

            Rectangle {
                id: divider1
                anchors.top: searchRow.bottom
                anchors.topMargin: 10
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: "#222"
            }

            Row {
                anchors.top: divider1.bottom
                anchors.topMargin: 10
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 16

                Column {
                    width: (parent.width - 32) / 3
                    spacing: 12

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "THEME"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Row {
                            spacing: 5

                            Repeater {
                                model: [
                                    { id: "dark",  label: "Dark",  bg: "#1a1a1a", fg: "#e0e0e0", bd: "#444" },
                                    { id: "light", label: "Light", bg: "#f5f5f5", fg: "#222",    bd: "#bbb" },
                                    { id: "sepia", label: "Sepia", bg: "#f4e4c1", fg: "#3b2a1a", bd: "#c8a96e" }
                                ]

                                delegate: Rectangle {
                                    width: thLbl.implicitWidth + 14
                                    height: 26
                                    radius: 13
                                    color: modelData.bg

                                    border.color: readerPage.currentTheme === modelData.id ? "#4fc3f7" : modelData.bd
                                    border.width: readerPage.currentTheme === modelData.id ? 2 : 1

                                    Text {
                                        id: thLbl
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: modelData.fg
                                        font.family: "Consolas"
                                        font.pixelSize: 11
                                        font.bold: readerPage.currentTheme === modelData.id
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor

                                        onClicked: {
                                            readerPage.currentTheme = modelData.id

                                            webView.executeScript(
                                                "if(window.setEpubTheme)window.setEpubTheme("
                                                + JSON.stringify(modelData.id)
                                                + ")"
                                            )

                                            readerPage.saveAllSettings()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "FONT SIZE"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Row {
                            spacing: 6

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: fDMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "A"
                                    color: "#aaa"
                                    font.pixelSize: 10
                                }

                                MouseArea {
                                    id: fDMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentFontSize = Math.max(60, readerPage.currentFontSize - 10)

                                        webView.executeScript(
                                            "if(window.setEpubFontSize)window.setEpubFontSize("
                                            + readerPage.currentFontSize
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: readerPage.currentFontSize + "%"
                                color: "#4fc3f7"
                                font.family: "Consolas"
                                font.bold: true
                                font.pixelSize: 12
                                width: 40
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: fIMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "A"
                                    color: "#ddd"
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                MouseArea {
                                    id: fIMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentFontSize = Math.min(200, readerPage.currentFontSize + 10)

                                        webView.executeScript(
                                            "if(window.setEpubFontSize)window.setEpubFontSize("
                                            + readerPage.currentFontSize
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "BRIGHTNESS"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Row {
                            spacing: 6

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: bDMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "−"
                                    color: "#aaa"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: bDMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentBrightness = Math.max(30, readerPage.currentBrightness - 10)

                                        webView.executeScript(
                                            "if(window.setEpubBrightness)window.setEpubBrightness("
                                            + readerPage.currentBrightness
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: readerPage.currentBrightness + "%"
                                color: "#4fc3f7"
                                font.family: "Consolas"
                                font.bold: true
                                font.pixelSize: 12
                                width: 40
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: bIMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: "#ddd"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: bIMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentBrightness = Math.min(200, readerPage.currentBrightness + 10)

                                        webView.executeScript(
                                            "if(window.setEpubBrightness)window.setEpubBrightness("
                                            + readerPage.currentBrightness
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    width: (parent.width - 32) / 3
                    spacing: 12

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "TEXT COLOR"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Flow {
                            width: parent.width
                            spacing: 5

                            Repeater {
                                model: [
                                    { label: "Auto",  color: "",        preview: "#e0e0e0" },
                                    { label: "White", color: "#ffffff", preview: "#ffffff" },
                                    { label: "Cream", color: "#f5f0e8", preview: "#f5f0e8" },
                                    { label: "Gray",  color: "#aaaaaa", preview: "#aaaaaa" },
                                    { label: "Black", color: "#111111", preview: "#111111" }
                                ]

                                delegate: Rectangle {
                                    width: tcLbl.implicitWidth + 18
                                    height: 26
                                    radius: 13
                                    color: "#1e1e1e"

                                    border.color: readerPage.currentTextColor === modelData.color ? "#4fc3f7" : "#333"
                                    border.width: readerPage.currentTextColor === modelData.color ? 2 : 1

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4

                                        Rectangle {
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: modelData.preview
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        Text {
                                            id: tcLbl
                                            text: modelData.label
                                            color: modelData.preview
                                            font.family: "Consolas"
                                            font.pixelSize: 11
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor

                                        onClicked: {
                                            readerPage.currentTextColor = modelData.color

                                            webView.executeScript(
                                                "if(window.setEpubTextColor)window.setEpubTextColor("
                                                + JSON.stringify(modelData.color)
                                                + ")"
                                            )

                                            readerPage.saveAllSettings()
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: 60
                                height: 26
                                radius: 13
                                color: custColMa.containsMouse ? "#1e2a3a" : "#1a1a1a"
                                border.color: "#4fc3f7"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Custom ⊕"
                                    color: "#4fc3f7"
                                    font.family: "Consolas"
                                    font.pixelSize: 10
                                }

                                MouseArea {
                                    id: custColMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.colorPickerOverlayVisible = true
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "MARGIN"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Row {
                            spacing: 6

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: pDMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "−"
                                    color: "#aaa"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: pDMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentPadding = readerPage.clampPadding(readerPage.currentPadding - 2)

                                        webView.executeScript(
                                            "if(window.setEpubPadding)window.setEpubPadding("
                                            + readerPage.currentPadding
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: readerPage.currentPadding + "px"
                                color: "#4fc3f7"
                                font.family: "Consolas"
                                font.bold: true
                                font.pixelSize: 12
                                width: 54
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: pIMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: "#ddd"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: pIMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentPadding = readerPage.clampPadding(readerPage.currentPadding + 2)

                                        webView.executeScript(
                                            "if(window.setEpubPadding)window.setEpubPadding("
                                            + readerPage.currentPadding
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: 5

                        Text {
                            text: "LINE SPACING"
                            color: "#555"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Row {
                            spacing: 6

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: lsDMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "−"
                                    color: "#aaa"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: lsDMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentLineSpacing = readerPage.clampLineSpacing(readerPage.currentLineSpacing - 10)

                                        webView.executeScript(
                                            "if(window.setEpubLineSpacing)window.setEpubLineSpacing("
                                            + readerPage.currentLineSpacing
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: readerPage.currentLineSpacing + "%"
                                color: "#4fc3f7"
                                font.family: "Consolas"
                                font.bold: true
                                font.pixelSize: 12
                                width: 40
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: lsIMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                                border.color: "#333"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: "#ddd"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    id: lsIMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentLineSpacing = readerPage.clampLineSpacing(readerPage.currentLineSpacing + 10)

                                        webView.executeScript(
                                            "if(window.setEpubLineSpacing)window.setEpubLineSpacing("
                                            + readerPage.currentLineSpacing
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    width: (parent.width - 32) / 3
                    spacing: 8

                    Text {
                        text: "FONT FAMILY"
                        color: "#555"
                        font.family: "Consolas"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1
                    }

                    Rectangle {
                        width: parent.width
                        height: 32
                        radius: 8
                        color: "#111"

                        border.color: fontSearchField.activeFocus ? "#4fc3f7" : "#2a2a2a"
                        border.width: 1

                        TextField {
                            id: fontSearchField
                            anchors.fill: parent
                            anchors.leftMargin: 32
                            anchors.rightMargin: 8

                            color: "#e0e0e0"
                            font.family: "Consolas"
                            font.pixelSize: 13

                            placeholderText: "Search or type font name..."
                            selectByMouse: true
                            activeFocusOnPress: true

                            background: Rectangle { color: "transparent" }

                            onPressed: readerPage.reclaimInputFocus(fontSearchField)

                            onTextChanged: readerPage.fontSearchText = text

                            Keys.onReturnPressed: {
                                if (text.trim() !== "") {
                                    readerPage.currentFontFamily = text.trim()

                                    webView.executeScript(
                                        "if(window.setEpubFont)window.setEpubFont("
                                        + JSON.stringify(text.trim())
                                        + ")"
                                    )

                                    readerPage.saveAllSettings()
                                }
                            }
                        }

                        Text {
                            text: "🔍"
                            color: "#555"
                            font.pixelSize: 14
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: 6

                        Repeater {
                            model: [
                                { label: "Default", value: ""                },
                                { label: "Georgia", value: "Georgia"         },
                                { label: "Times",   value: "Times New Roman" },
                                { label: "Arial",   value: "Arial"           },
                                { label: "Cambria", value: "Cambria"         },
                                { label: "Verdana", value: "Verdana"         }
                            ]

                            delegate: Rectangle {
                                width: ffLbl.implicitWidth + 16
                                height: 30
                                radius: 15

                                color: readerPage.currentFontFamily === modelData.value ? "#1e2a3a" : "#1a1a1a"
                                border.color: readerPage.currentFontFamily === modelData.value ? "#4fc3f7" : "#2a2a2a"
                                border.width: 1

                                Text {
                                    id: ffLbl
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: readerPage.currentFontFamily === modelData.value ? "#4fc3f7" : "#777"
                                    font.family: modelData.value === "" ? "Consolas" : modelData.value
                                    font.pixelSize: 13
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor

                                    onClicked: {
                                        readerPage.currentFontFamily = modelData.value

                                        webView.executeScript(
                                            "if(window.setEpubFont)window.setEpubFont("
                                            + JSON.stringify(modelData.value)
                                            + ")"
                                        )

                                        readerPage.saveAllSettings()
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        width: parent.width
                        implicitHeight: 140
                        clip: true
                        spacing: 4

                        model: {
                            var fonts = BookManager.availableFonts();

                            if (!readerPage.fontSearchText)
                                return fonts;

                            var st = readerPage.fontSearchText.toLowerCase();

                            return fonts.filter(function(f) {
                                return f.toLowerCase().indexOf(st) !== -1;
                            });
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded

                            contentItem: Rectangle {
                                implicitWidth: 4
                                radius: 2
                                color: "#333333"
                            }

                            background: Rectangle {
                                color: "transparent"
                            }
                        }

                        delegate: Rectangle {
                            width: parent.width
                            height: 28
                            radius: 6

                            color: readerPage.currentFontFamily === modelData
                                   ? "#1e2a3a"
                                   : (fontItemMa.containsMouse ? "#222222" : "#141414")

                            border.color: readerPage.currentFontFamily === modelData
                                          ? "#4fc3f7"
                                          : (fontItemMa.containsMouse ? "#333333" : "#222222")

                            border.width: 1

                            Behavior on color { ColorAnimation { duration: 80 } }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                color: readerPage.currentFontFamily === modelData ? "#4fc3f7" : "#cccccc"
                                font.family: "Consolas"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                id: fontItemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor

                                onClicked: {
                                    readerPage.currentFontFamily = modelData

                                    webView.executeScript(
                                        "if(window.setEpubFont)window.setEpubFont("
                                        + JSON.stringify(modelData)
                                        + ")"
                                    )

                                    readerPage.saveAllSettings()
                                }
                            }
                        }
                    }

                    Text {
                        text: "Drop .ttf/.otf in <exe>/fonts/"
                        color: "#3a3a3a"
                        font.family: "Consolas"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    Rectangle {
        id: controlsBar

        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.bottom: parent.bottom

        height: 56
        color: "#181818"
        z: 100
        clip: true

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * readerPage.progressPct
            z: 0

            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop { position: 0.0; color: "#334fc3f7" }
                GradientStop { position: 0.7; color: "#1a4fc3f7" }
                GradientStop { position: 1.0; color: "#004fc3f7" }
            }

            Behavior on width {
                NumberAnimation { duration: 300; easing.type: Easing.OutQuad }
            }
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: "#242424"
            z: 1
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10
            z: 2

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: backMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                border.color: "#2e2e2e"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: "\u2190"
                    color: "#aaa"
                    font.pixelSize: 16
                }

                MouseArea {
                    id: backMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: {
                        settingsPanel.open = false
                        readerPage.colorPickerOverlayVisible = false
                        readerPage.pageInputActive = false

                        if (readerPage.bookFormat === "PDF") {
                            readerPage.isClosing = true
                            readerPage.saveCurrentPdfProgress()
                            closeTimer.start()
                        } else {
                            readerPage.closeRequested()
                        }
                    }
                }
            }

            Rectangle {
                width: 36
                height: 36
                radius: 18
                visible: readerPage.bookFormat === "EPUB"
                color: tocMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                border.color: "#2e2e2e"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: "\u2630"
                    color: "#aaa"
                    font.pixelSize: 14
                }

                MouseArea {
                    id: tocMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: webView.executeScript("window.toggleToc && window.toggleToc()")
                }
            }

            Row {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: readerPage.bookTitle
                    color: "#d0d0d0"
                    font.family: "Consolas"
                    font.bold: true
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                // Interactive Editable Page Control for EPUB & PDF
                Item {
                    id: pageControl
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: pageInputRow.visible ? pageInputRow.implicitWidth : progText.implicitWidth
                    implicitHeight: 32

                    // Mode 1: Clickable Page Label
                    Text {
                        id: progText
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !readerPage.pageInputActive

                        text: {
                            if (readerPage.currentPage > 0 && readerPage.totalPages > 0)
                                return "Page " + readerPage.currentPage + " / " + readerPage.totalPages
                            else if (readerPage.progressPct > 0)
                                return Math.round(readerPage.progressPct * 100) + "%"

                            return "Page 1"
                        }

                        color: progMa.containsMouse ? "#81d4fa" : "#4fc3f7"
                        font.family: "Consolas"
                        font.bold: true
                        font.pixelSize: 13

                        Behavior on color { ColorAnimation { duration: 100 } }

                        MouseArea {
                            id: progMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                readerPage.pageInputActive = true
                                pageInputField.text = readerPage.currentPage > 0 ? readerPage.currentPage.toString() : "1"
                                pageInputField.forceActiveFocus()
                                pageInputField.selectAll()
                            }
                        }
                    }

                    // Mode 2: Interactive Page Number Input
                    RowLayout {
                        id: pageInputRow
                        anchors.verticalCenter: parent.verticalCenter
                        visible: readerPage.pageInputActive
                        spacing: 6

                        Text {
                            text: "Page"
                            color: "#81d4fa"
                            font.family: "Consolas"
                            font.bold: true
                            font.pixelSize: 13
                        }

                        Rectangle {
                            width: Math.max(48, pageInputField.implicitWidth + 16)
                            height: 28
                            radius: 6
                            color: "#111111"
                            border.color: pageInputField.activeFocus ? "#4fc3f7" : "#333333"
                            border.width: 1

                            TextField {
                                id: pageInputField
                                anchors.fill: parent
                                anchors.margins: 2
                                color: "#ffffff"
                                font.family: "Consolas"
                                font.bold: true
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                maximumLength: 5

                                background: Rectangle { color: "transparent" }

                                Keys.onReturnPressed: readerPage.jumpToPage(text)
                                Keys.onEnterPressed:  readerPage.jumpToPage(text)
                                Keys.onEscapePressed: {
                                    readerPage.pageInputActive = false
                                    readerPage.reclaimInputFocus(null)
                                }

                                onEditingFinished: {
                                    if (readerPage.pageInputActive)
                                        readerPage.jumpToPage(text)
                                }
                            }
                        }

                        Text {
                            text: "/ " + (readerPage.totalPages > 0 ? readerPage.totalPages : "100")
                            color: "#aaa"
                            font.family: "Consolas"
                            font.pixelSize: 13
                        }
                    }
                }
            }

            Rectangle {
                width: 36
                height: 36
                radius: 18
                visible: readerPage.bookFormat === "EPUB"
                color: settingsPanel.open ? "#1e2a3a" : (gearMa.containsMouse ? "#2a2a2a" : "#1e1e1e")
                border.color: settingsPanel.open ? "#4fc3f7" : "#2e2e2e"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: "\u2699"
                    color: settingsPanel.open ? "#4fc3f7" : "#aaa"
                    font.pixelSize: 16

                    Behavior on color { ColorAnimation { duration: 120 } }

                    RotationAnimator on rotation {
                        running: settingsPanel.open
                        from: 0
                        to: 360
                        duration: 8000
                        loops: Animation.Infinite
                    }
                }

                MouseArea {
                    id: gearMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: {
                        settingsPanel.open = !settingsPanel.open
                        readerPage.colorPickerOverlayVisible = false
                        readerPage.pageInputActive = false
                    }
                }
            }

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: fsMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                border.color: "#2e2e2e"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: typeof appWindow !== "undefined" && appWindow && appWindow.visibility === Window.FullScreen ? "\u2716" : "\u26F6"
                    color: "#aaa"
                    font.pixelSize: 14
                }

                MouseArea {
                    id: fsMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: {
                        if (typeof appWindow !== "undefined" && appWindow) {
                            if (appWindow.visibility === Window.FullScreen)
                                appWindow.showNormal()
                            else
                                appWindow.showFullScreen()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: savingOverlay
        anchors.fill: parent
        color: "#d0000000"
        visible: readerPage.isClosing
        z: 99999

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 12

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: readerPage.isClosing

                contentItem: Rectangle {
                    implicitWidth: 40
                    implicitHeight: 40
                    color: "transparent"
                    border.color: "#4fc3f7"
                    border.width: 3
                    radius: 20

                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 1000
                        loops: Animation.Infinite
                        running: readerPage.isClosing
                    }
                }
            }

            Text {
                text: "Saving changes natively..."
                color: "#e0e0e0"
                font.family: "Consolas"
                font.pixelSize: 13
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    Item {
        anchors.fill: parent
        focus: true

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                if (readerPage.colorPickerOverlayVisible) {
                    readerPage.colorPickerOverlayVisible = false
                    event.accepted = true
                } else if (readerPage.pageInputActive) {
                    readerPage.pageInputActive = false
                    readerPage.reclaimInputFocus(null)
                    event.accepted = true
                } else if (settingsPanel.open) {
                    settingsPanel.open = false
                    event.accepted = true
                } else if (typeof appWindow !== "undefined" && appWindow && appWindow.visibility === Window.FullScreen) {
                    appWindow.showNormal()
                    event.accepted = true
                } else {
                    if (readerPage.bookFormat === "PDF") {
                        readerPage.isClosing = true
                        readerPage.saveCurrentPdfProgress()
                        closeTimer.start()
                    } else {
                        readerPage.closeRequested()
                    }

                    event.accepted = true
                }
            } else if (event.key === Qt.Key_F) {
                if (typeof appWindow !== "undefined" && appWindow) {
                    if (appWindow.visibility === Window.FullScreen)
                        appWindow.showNormal()
                    else
                        appWindow.showFullScreen()
                }

                event.accepted = true
            } else if (event.key === Qt.Key_Comma) {
                readerPage.currentFontSize = Math.max(60, readerPage.currentFontSize - 10)

                webView.executeScript(
                    "if(window.setEpubFontSize)window.setEpubFontSize("
                    + readerPage.currentFontSize
                    + ")"
                )

                readerPage.saveAllSettings()
                event.accepted = true
            } else if (event.key === Qt.Key_Period) {
                readerPage.currentFontSize = Math.min(200, readerPage.currentFontSize + 10)

                webView.executeScript(
                    "if(window.setEpubFontSize)window.setEpubFontSize("
                    + readerPage.currentFontSize
                    + ")"
                )

                readerPage.saveAllSettings()
                event.accepted = true
            }
        }
    }
}
