import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Raksha_Hub

Item {
    id: playerRoot
    focus: true

    property string filePath:     ""
    property real   startMs:      0
    property int    entryId:      -1
    property int    season:       0
    property int    episode:      0
    property string showTitle:    ""
    property string episodeTitle: ""
    property bool   isLoading:    false


    property bool isLocked: false

    onIsLockedChanged: {
        playerRoot.forceActiveFocus()
    }

    signal playbackStopped(int entryId, int season, int episode,
                           real posMs, real durMs)

    property bool renderContextReady: false
    property bool playPending:        false

    property int  m_switchCount:   0
    property real savedVolume:     100
    property bool stableIsPlaying: false

    property real preHoldSpeed:    1.0
    property bool holdBoosting:    false

    property int  scrubFrameCount: 0
    property int  scrubInterval:   10
    property bool scrubReady:      false
    property bool scrubRequested:  false

    property bool subtitleAutoLoadRequested: false

    property real subtitleFontSize: 48
    readonly property real minSubtitleFontSize: 16
    readonly property real maxSubtitleFontSize: 96

    function applySubtitleSize(px) {
        px = Math.round(Math.max(playerRoot.minSubtitleFontSize,
                         Math.min(px, playerRoot.maxSubtitleFontSize)))
        playerRoot.subtitleFontSize = px
        LibraryManager.saveSetting("subtitle_font_size", px.toString())
        mpv.setSubtitleFontSize(px)
    }

    onFilePathChanged: {
        scrubReady                = false
        scrubFrameCount           = 0
        scrubRequested            = false
        subtitleAutoLoadRequested = false
        isLocked                  = false
    }

    function tryAutoLoadSubtitle() {
        if (playerRoot.subtitleAutoLoadRequested) return
        if (playerRoot.entryId < 0) return
        playerRoot.subtitleAutoLoadRequested = true
        var saved = playerRoot.season > 0
            ? LibraryManager.episodeSubtitle(playerRoot.entryId, playerRoot.season, playerRoot.episode)
            : LibraryManager.movieSubtitle(playerRoot.entryId)
        if (saved && saved.length > 0)
            mpv.addSubtitleFile(saved)
    }

    function tryRequestScrubThumbnails() {
        if (playerRoot.scrubRequested) return
        if (!mpv.hasVideo || playerRoot.entryId < 0 || playerRoot.filePath.length === 0) return
        if (mpv.duration <= 0) {
            scrubRetryTimer.restart()
            return
        }
        playerRoot.scrubRequested = true
        LibraryManager.ensureScrubThumbnails(
            playerRoot.entryId, playerRoot.season, playerRoot.episode,
            playerRoot.filePath, mpv.duration)
    }

    Timer {
        id: scrubRetryTimer
        interval: 200
        onTriggered: playerRoot.tryRequestScrubThumbnails()
    }

    Timer {
        id: playDebounce
        interval: 500
        onTriggered: {
            if (!mpv.playing && playerRoot.m_switchCount === 0)
                playerRoot.stableIsPlaying = false
        }
    }

    Component.onCompleted: {
        var v = parseFloat(LibraryManager.getSetting("volume", "100"))
        playerRoot.savedVolume = isNaN(v) ? 100 : v

        var s = parseFloat(LibraryManager.getSetting("subtitle_font_size", "48"))
        playerRoot.subtitleFontSize = isNaN(s) ? 48 : s
    }

    readonly property var nextEp: (season > 0 && entryId >= 0)
        ? LibraryManager.nextEpisode(entryId, season, episode)
        : ({ "exists": false })

    function saveProgress() {
        if (playerRoot.entryId < 0) return
        if (playerRoot.season === 0 && playerRoot.episode === 0)
            LibraryManager.updateMovieProgress(
                playerRoot.entryId, mpv.position, mpv.duration)
        else
            LibraryManager.updateEpisodeProgress(
                playerRoot.entryId, playerRoot.season, playerRoot.episode,
                mpv.position, mpv.duration)
    }

    function playNextEpisode() {
        if (!nextEp || !nextEp.exists) return

        var ep = {
            filePath:   nextEp.filePath,
            positionMs: nextEp.positionMs || 0,
            season:     nextEp.season,
            episode:    nextEp.episode,
            title:      nextEp.title || ""
        }

        if (ep.season > 0 && playerRoot.entryId >= 0)
            LibraryManager.saveSetting("last_ep_" + playerRoot.entryId, ep.season + "," + ep.episode)

        saveProgress()

        playerRoot.m_switchCount++
        playerRoot.isLoading    = true
        playerRoot.episodeTitle = ep.title

        appWindow.playerStartMs  = ep.positionMs
        appWindow.playerSeason   = ep.season
        appWindow.playerEpisode  = ep.episode
        appWindow.playerFilePath = ep.filePath

        playerRoot.stableIsPlaying = true
        mpv.play(ep.filePath, ep.positionMs)

        showControls()
    }

    onVisibleChanged: {
        if (visible) {
            isLocked        = false
            controlsVisible = true
            hideTimer.restart()
            playerRoot.forceActiveFocus()

            mpv.setLowMemoryMode(true) // Caps demuxer memory usage

            if (filePath.length === 0) return

            playerRoot.stableIsPlaying = true
            if (renderContextReady)
                mpv.play(filePath, startMs)
            else
                playPending = true
        } else {
            mpv.stop()
            playPending = false
            isLoading   = false
            isLocked    = false
        }
    }

    function saveAndClose() {
        playerRoot.playbackStopped(
            playerRoot.entryId, playerRoot.season, playerRoot.episode,
            mpv.position, mpv.duration)
        appWindow.playerOpen = false
        gc()
    }

    function toggleFullscreen() {
        if (appWindow.visibility === Window.FullScreen)
            appWindow.showNormal()
        else
            appWindow.showFullScreen()
    }

    property bool controlsVisible: true

    function showControls() {
        if (isLocked) return
        controlsVisible = true
        hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: {
            if (mpv.playing) controlsVisible = false
            else restart()
        }
    }

    Timer {
        interval: 5000
        repeat: true
        running: mpv.playing
        onTriggered: playerRoot.saveProgress()
    }

    Connections {
        target: mpv
        function onVolumeChanged() {
            playerRoot.savedVolume = mpv.volume
            LibraryManager.saveSetting("volume", mpv.volume.toString())
        }
        function onDurationChanged()  { playerRoot.tryRequestScrubThumbnails() }
        function onHasVideoChanged() {
            if (mpv.hasVideo) {
                playerRoot.tryRequestScrubThumbnails()
                playerRoot.tryAutoLoadSubtitle()
                mpv.setSubtitleFontSize(playerRoot.subtitleFontSize)
            } else {
                playerRoot.subtitleAutoLoadRequested = false
            }
        }
    }

    Connections {
        target: LibraryManager
        function onScrubThumbnailsReady(entryId, season, episode, frameCount, intervalSec) {
            if (entryId === playerRoot.entryId && season === playerRoot.season && episode === playerRoot.episode) {
                playerRoot.scrubFrameCount = frameCount
                playerRoot.scrubInterval   = intervalSec
                playerRoot.scrubReady      = true
            }
        }
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    MpvPlayer {
        id: mpv
        width:  playerRoot.visible ? playerRoot.width  : 1
        height: playerRoot.visible ? playerRoot.height : 1

        onRenderReady: {
            playerRoot.renderContextReady = true
            mpv.setVolume(playerRoot.savedVolume)
            mpv.setSubtitleFontSize(playerRoot.subtitleFontSize)
            if (playerRoot.playPending && playerRoot.visible && playerRoot.filePath.length > 0) {
                playerRoot.playPending     = false
                playerRoot.stableIsPlaying = true
                mpv.play(playerRoot.filePath, playerRoot.startMs)
            }
        }

        onEndReached: function(finalPos) {
            playerRoot.playbackStopped(
                playerRoot.entryId, playerRoot.season, playerRoot.episode,
                finalPos, mpv.duration)
            if (playerRoot.nextEp && playerRoot.nextEp.exists)
                playerRoot.playNextEpisode()
            else
                appWindow.playerOpen = false
        }

        onStopped: function(finalPos) {}

        onPlayingChanged: {
            if (mpv.playing) {
                if (playerRoot.m_switchCount > 0)
                    playerRoot.m_switchCount--
                playerRoot.stableIsPlaying = true
                playerRoot.isLoading       = false
                playDebounce.stop()
            } else {
                playDebounce.restart()
                if (playerRoot.m_switchCount === 0 && playerRoot.visible)
                    playerRoot.saveProgress()
            }
        }
    }

    MouseArea {
        id: backgroundMa
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: (controlsVisible && !playerRoot.isLocked) ? Qt.ArrowCursor : Qt.BlankCursor
        onMouseXChanged: playerRoot.showControls()
        onMouseYChanged: playerRoot.showControls()

        onPressed: function(mouse) {
            playerRoot.forceActiveFocus()
            if (playerRoot.isLocked) return
            if (mouse.button === Qt.RightButton) {
                playerRoot.preHoldSpeed = mpv.speed
                playerRoot.holdBoosting = true
                mpv.speed = 2.0
            }
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.RightButton && playerRoot.holdBoosting) {
                mpv.speed = playerRoot.preHoldSpeed
                playerRoot.holdBoosting = false
            }
        }
        onClicked: function(mouse) {
            playerRoot.forceActiveFocus()
            if (mouse.button !== Qt.LeftButton) return
            if (playerRoot.isLocked) return
            mpv.togglePause()
            playerRoot.showControls()
        }
        onDoubleClicked: {
            playerRoot.forceActiveFocus()
            if (playerRoot.isLocked) return
            playerRoot.toggleFullscreen()
        }
    }

    Timer {
        id: spaceHoldTimer
        interval: 350
        onTriggered: {
            playerRoot.preHoldSpeed = mpv.speed
            playerRoot.holdBoosting = true
            mpv.speed = 2.0
        }
    }

    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_L:
            playerRoot.isLocked = !playerRoot.isLocked
            if (playerRoot.isLocked) {
                playerRoot.controlsVisible = false
                subMenu.visible = false
                speedPanel.visible = false
            } else {
                playerRoot.showControls()
            }
            playerRoot.forceActiveFocus()
            event.accepted = true
            break
        case Qt.Key_O:
            appWindow.resetToDefaultWindow()
            playerRoot.showControls()
            event.accepted = true
            break
        case Qt.Key_Space:
            if (!event.isAutoRepeat) spaceHoldTimer.restart()
            event.accepted = true
            break
        case Qt.Key_F:
        case Qt.Key_F11:
            playerRoot.toggleFullscreen()
            event.accepted = true
            break
        case Qt.Key_Escape:
            if (appWindow.visibility === Window.FullScreen) appWindow.showNormal()
            else playerRoot.saveAndClose()
            event.accepted = true
            break
        case Qt.Key_Left:
            mpv.seek(Math.max(0, mpv.position - 5000))
            playerRoot.showControls()
            event.accepted = true
            break
        case Qt.Key_Right:
            mpv.seek(Math.min(mpv.duration, mpv.position + 5000))
            playerRoot.showControls()
            event.accepted = true
            break
        case Qt.Key_Up:
            mpv.setVolume(Math.min(130, mpv.volume + 5))
            playerRoot.showControls()
            event.accepted = true
            break
        case Qt.Key_Down:
            mpv.setVolume(Math.max(0, mpv.volume - 5))
            playerRoot.showControls()
            event.accepted = true
            break
        case Qt.Key_N:
            if (nextEp && nextEp.exists) {
                playerRoot.playNextEpisode()
                event.accepted = true
            }
            break
        }
    }

    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceHoldTimer.stop()
            if (playerRoot.holdBoosting) {
                mpv.speed = playerRoot.preHoldSpeed
                playerRoot.holdBoosting = false
            } else {
                mpv.togglePause()
                playerRoot.showControls()
            }
            event.accepted = true
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: subMenu.visible
        z: 50
        onClicked: subMenu.visible = false
    }

    FileDialog {
        id: subtitleFileDialog
        title: "Choose subtitle file"
        nameFilters: ["Subtitle files (*.srt *.ass *.ssa *.vtt *.sub)", "All files (*)"]
        onAccepted: {
            mpv.addSubtitleFile(selectedFile.toString())
            if (playerRoot.season > 0)
                LibraryManager.setEpisodeSubtitle(
                    playerRoot.entryId, playerRoot.season, playerRoot.episode,
                    selectedFile.toString())
            else
                LibraryManager.setMovieSubtitle(
                    playerRoot.entryId, selectedFile.toString())
            subMenu.visible = false
        }
    }

    Rectangle {
        id: subMenu
        visible: false
        z: 51

        readonly property int rowH:       38
        readonly property int rowSpacing: 2
        readonly property int maxRows:    6
        readonly property int pad:        8
        readonly property int n:          Math.min(mpv.subtitleTracks.length, maxRows) + 1
        readonly property int sizeRowH:   58
        readonly property int dividerH:   1

        width: 220
        height: (n > 0 ? n * rowH + Math.max(0, n - 1) * rowSpacing : 0) + rowSpacing + dividerH + rowSpacing + sizeRowH + pad * 2
        radius: 8; color: "#1c1c1c"
        border.color: "#444"; border.width: 1

        anchors.right:       parent.right
        anchors.rightMargin: 20
        y: playerRoot.height - height - 70
        clip: true

        Flickable {
            anchors.fill: parent
            anchors.margins: subMenu.pad
            contentHeight: subCol.implicitHeight
            clip: true
            flickDeceleration: 800
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: subCol
                width: parent.width
                spacing: subMenu.rowSpacing

                Rectangle {
                    width: parent.width; height: subMenu.rowH; radius: 5
                    color: addSubMa.containsMouse ? "#2e2e2e" : "transparent"

                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: "+ Add subtitle file\u2026"; color: "#5aa4ff"; font.family: "Consolas"; font.bold: true; font.pixelSize: 13
                    }

                    MouseArea {
                        id: addSubMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: subtitleFileDialog.open()
                    }
                }

                Rectangle { width: parent.width; height: 1; color: "#333" }

                Item {
                    width: parent.width; height: subMenu.sizeRowH

                    Text {
                        id: sizeLabel; anchors.top: parent.top; anchors.left: parent.left; anchors.leftMargin: 10
                        text: "Size \u2014 " + Math.round(playerRoot.subtitleFontSize) + "px"; color: "#999"; font.family: "Consolas"; font.pixelSize: 11
                    }

                    Item {
                        anchors.top: sizeLabel.bottom; anchors.topMargin: 8; anchors.left: parent.left; anchors.leftMargin: 10
                        width: parent.width - 20; height: 20

                        Rectangle {
                            id: sizeTrack; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right
                            height: sizeTrackMa.containsMouse ? 5 : 3; radius: 3; color: "#33ffffff"

                            Rectangle {
                                width: parent.width * ((playerRoot.subtitleFontSize - playerRoot.minSubtitleFontSize) / (playerRoot.maxSubtitleFontSize - playerRoot.minSubtitleFontSize))
                                height: parent.height; radius: parent.radius; color: "#4a7fc1"
                            }

                            Rectangle {
                                x: sizeTrack.width * ((playerRoot.subtitleFontSize - playerRoot.minSubtitleFontSize) / (playerRoot.maxSubtitleFontSize - playerRoot.minSubtitleFontSize)) - width / 2
                                anchors.verticalCenter: parent.verticalCenter; width: sizeTrackMa.containsMouse ? 14 : 10; height: width; radius: width / 2; color: "white"
                            }
                        }

                        MouseArea {
                            id: sizeTrackMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; preventStealing: true
                            function pxFromX(px) {
                                var frac = Math.max(0, Math.min(px / width, 1))
                                return playerRoot.minSubtitleFontSize + frac * (playerRoot.maxSubtitleFontSize - playerRoot.minSubtitleFontSize)
                            }
                            onPressed: function(m) { playerRoot.applySubtitleSize(sizeTrackMa.pxFromX(m.x)) }
                            onPositionChanged: function(m) { if (pressed) playerRoot.applySubtitleSize(sizeTrackMa.pxFromX(m.x)) }
                        }
                    }
                }

                Repeater {
                    model: mpv.subtitleTracks
                    delegate: Rectangle {
                        width: parent.width; height: subMenu.rowH; radius: 5
                        color: subItemMa.containsMouse ? "#2e2e2e" : "transparent"

                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title || ("Track " + modelData.id); color: "white"; font.family: "Consolas"; font.pixelSize: 13
                            elide: Text.ElideRight; width: parent.width - 20
                        }

                        MouseArea {
                            id: subItemMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { mpv.setSubtitleTrack(modelData.id); subMenu.visible = false }
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: speedPanel.visible
        z: 50
        onClicked: speedPanel.visible = false
    }

    Rectangle {
        id: speedPanel
        visible: false
        z: 51

        readonly property real minSpeed: 0.25
        readonly property real maxSpeed: 2.0

        width: 230; height: 118; radius: 8; color: "#1c1c1c"; border.color: "#444"; border.width: 1
        anchors.right: parent.right; anchors.rightMargin: 20
        y: playerRoot.height - height - 70

        Column {
            anchors.fill: parent; anchors.margins: 14; spacing: 12

            Text { text: "Playback speed  \u2014  " + (Math.round(mpv.speed * 100) / 100) + "x"; color: "white"; font.family: "Consolas"; font.pixelSize: 13 }

            Item {
                width: parent.width; height: 22

                Rectangle {
                    id: speedTrack; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right
                    height: speedTrackMa.containsMouse ? 5 : 3; radius: 3; color: "#33ffffff"

                    Rectangle {
                        width: parent.width * ((mpv.speed - speedPanel.minSpeed) / (speedPanel.maxSpeed - speedPanel.minSpeed))
                        height: parent.height; radius: parent.radius; color: "#4a7fc1"
                    }

                    Rectangle {
                        x: speedTrack.width * ((mpv.speed - speedPanel.minSpeed) / (speedPanel.maxSpeed - speedPanel.minSpeed)) - width / 2
                        anchors.verticalCenter: parent.verticalCenter; width: speedTrackMa.containsMouse ? 14 : 10; height: width; radius: width / 2; color: "white"
                    }
                }

                MouseArea {
                    id: speedTrackMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; preventStealing: true
                    function speedFromX(px) {
                        var frac = Math.max(0, Math.min(px / width, 1))
                        return speedPanel.minSpeed + frac * (speedPanel.maxSpeed - speedPanel.minSpeed)
                    }
                    onPressed: function(m) { mpv.speed = speedTrackMa.speedFromX(m.x) }
                    onPositionChanged: function(m) { if (pressed) mpv.speed = speedTrackMa.speedFromX(m.x) }
                }
            }

            Row {
                spacing: 6
                Repeater {
                    model: [0.5, 1.0, 1.25, 1.5, 2.0]
                    delegate: Rectangle {
                        width: presetLabel.implicitWidth + 14; height: 22; radius: 11
                        color: Math.abs(mpv.speed - modelData) < 0.01 ? "#4a7fc1" : (presetMa.containsMouse ? "#33ffffff" : "#22ffffff")
                        Text { id: presetLabel; anchors.centerIn: parent; text: modelData + "x"; color: "white"; font.family: "Consolas"; font.pixelSize: 11 }
                        MouseArea {
                            id: presetMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: mpv.speed = modelData
                        }
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        opacity: (playerRoot.controlsVisible && !playerRoot.isLocked) ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Rectangle {
            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 100
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#cc000000" }
                GradientStop { position: 1.0; color: "#00000000" }
            }

            MouseArea { anchors.fill: parent; onClicked: {} }

            Row {
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 18; spacing: 14

                Rectangle {
                    width: 36; height: 36; radius: 18; color: backMa.containsMouse ? "#55ffffff" : "#22ffffff"
                    Text { anchors.centerIn: parent; text: "\u2190"; color: "white"; font.pixelSize: 18 }
                    MouseArea { id: backMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: playerRoot.saveAndClose() }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter; spacing: 3
                    Text { visible: playerRoot.showTitle.length > 0; text: playerRoot.showTitle; color: "#ffffff"; font.family: "Consolas"; font.bold: true; font.pixelSize: 14 }
                    Text {
                        visible: playerRoot.season > 0
                        text: "S" + playerRoot.season + " · E" + String(playerRoot.episode).padStart(2, "0") + (playerRoot.episodeTitle.length > 0 ? "  " + playerRoot.episodeTitle : "")
                        color: "#aaaaaa"; font.family: "Consolas"; font.pixelSize: 12
                    }
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 150
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: "#dd000000" }
            }

            MouseArea { anchors.fill: parent; onClicked: {} }

            ColumnLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20; spacing: 10

                Item {
                    Layout.fillWidth: true; height: 20
                    property real hoverFrac: 0

                    Rectangle {
                        id: seekTrack; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right
                        height: seekMa.containsMouse ? 5 : 3; radius: 3; color: "#33ffffff"

                        Rectangle {
                            width: mpv.duration > 0 ? parent.width * (mpv.position / mpv.duration) : 0
                            height: parent.height; radius: parent.radius; color: "#4a7fc1"
                        }

                        Rectangle {
                            x: mpv.duration > 0 ? (seekTrack.width * (mpv.position / mpv.duration)) - width / 2 : -width / 2
                            anchors.verticalCenter: parent.verticalCenter; width: seekMa.containsMouse ? 14 : 0; height: width; radius: width / 2; color: "white"
                        }
                    }

                    MouseArea {
                        id: seekMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; preventStealing: true
                        onPressed: function(m) { mpv.seek((m.x / width) * mpv.duration) }
                        onPositionChanged: function(m) {
                            parent.hoverFrac = Math.max(0, Math.min(m.x / width, 1))
                            if (pressed) mpv.seek(parent.hoverFrac * mpv.duration)
                        }
                    }

                    Rectangle {
                        id: scrubPreview
                        visible: (seekMa.containsMouse || seekMa.pressed) && playerRoot.scrubReady && mpv.duration > 0 && playerRoot.scrubFrameCount > 0
                        width: 172; height: 116; radius: 6; color: "#000000"; border.color: "#555555"; border.width: 1
                        y: -132
                        x: Math.max(0, Math.min(parent.width - width, parent.hoverFrac * parent.width - width / 2))

                        readonly property real hoverMs:    parent.hoverFrac * mpv.duration
                        readonly property int  frameIndex: Math.min(playerRoot.scrubFrameCount, Math.max(1, Math.round(hoverMs / 1000 / playerRoot.scrubInterval) + 1))
                        readonly property string thumbSource: {
                            var dir = LibraryManager.scrubCacheDir(playerRoot.entryId, playerRoot.season, playerRoot.episode)
                            return "file:///" + dir.replace(/\\/g, "/") + "/thumb_" + String(frameIndex).padStart(4, "0") + ".jpg"
                        }

                        Image {
                            anchors.fill: parent; anchors.margins: 3
                            source: scrubPreview.visible ? scrubPreview.thumbSource : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true; cache: false
                            sourceSize.width: 135
                        }

                        Text {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 4
                            text: {
                                var s   = Math.floor(scrubPreview.hoverMs / 1000)
                                var h   = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
                                return (h > 0 ? (h + ":" + String(m).padStart(2, "0")) : String(m)) + ":" + String(sec).padStart(2, "0")
                            }
                            color: "white"; font.family: "Consolas"; font.bold: true; font.pixelSize: 12
                            style: Text.Outline; styleColor: "#000000"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 14

                    Rectangle {
                        width: 44; height: 44; radius: 22
                        color: playMa.containsMouse ? "#40ffffff" : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: (playerRoot.stableIsPlaying || playerRoot.m_switchCount > 0) ? "\u23F8" : "\u25B6"
                            color: "white"; font.pixelSize: (playerRoot.stableIsPlaying || playerRoot.m_switchCount > 0) ? 20 : 18
                        }
                        MouseArea { id: playMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mpv.togglePause() }
                    }

                    Rectangle {
                        width: 36; height: 36; radius: 18; color: skipBMa.containsMouse ? "#33ffffff" : "transparent"
                        Text { anchors.centerIn: parent; text: "\u21BA"; color: "white"; font.pixelSize: 16 }
                        MouseArea { id: skipBMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mpv.seek(Math.max(0, mpv.position - 5000)) }
                    }

                    Rectangle {
                        width: 36; height: 36; radius: 18; color: skipFMa.containsMouse ? "#33ffffff" : "transparent"
                        Text { anchors.centerIn: parent; text: "\u21BB"; color: "white"; font.pixelSize: 16 }
                        MouseArea { id: skipFMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mpv.seek(Math.min(mpv.duration, mpv.position + 5000)) }
                    }

                    Text {
                        text: formatMs(mpv.position) + "  /  " + formatMs(mpv.duration)
                        color: "#ccffffff"; font.family: "Consolas"; font.pixelSize: 13
                        function formatMs(ms) {
                            var s = Math.floor(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
                            if (h > 0) return h + ":" + String(m).padStart(2, "0") + ":" + String(sec).padStart(2, "0")
                            return String(m).padStart(2, "0") + ":" + String(sec).padStart(2, "0")
                        }
                    }

                    Rectangle {
                        visible: nextEp && nextEp.exists
                        height: 37; width: nextEpLabel.implicitWidth + 28; radius: 20
                        color: (nextEp && nextEp.isNextSeason) ? (nextEpMa.containsMouse ? "#3da8c8" : "#2e8aa8") : (nextEpMa.containsMouse ? "#22ffffff" : "transparent")
                        border.color: (nextEp && nextEp.isNextSeason) ? "transparent" : "#4fc3f7"
                        border.width: (nextEp && nextEp.isNextSeason) ? 0 : 1

                        Text {
                            id: nextEpLabel; anchors.centerIn: parent
                            text: (nextEp && nextEp.isNextSeason) ? "Next Season" : "Next Episode"
                            color: "white"; font.family: "Consolas"; font.bold: nextEp && nextEp.isNextSeason; font.pixelSize: 11
                        }

                        MouseArea { id: nextEpMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: playerRoot.playNextEpisode() }
                    }

                    Item { Layout.fillWidth: true }

                    Row {
                        spacing: 8; Layout.alignment: Qt.AlignVCenter
                        Text { text: mpv.volume === 0 ? "\uD83D\uDD07" : mpv.volume < 50 ? "\uD83D\uDD08" : "\uD83D\uDD0A"; color: "white"; font.pixelSize: 16 }
                        Item {
                            width: 80; height: 18
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 3; radius: 2; color: "#44ffffff"
                                Rectangle { width: parent.width * (mpv.volume / 100); height: parent.height; radius: 2; color: "white" }
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onPressed: function(m) { mpv.setVolume((m.x / width) * 100) }
                                onPositionChanged: function(m) { if (pressed) mpv.setVolume(Math.max(0, Math.min((m.x / width) * 100, 100))) }
                            }
                        }
                    }

                    Rectangle {
                        width: 50; height: 32; radius: 7; color: speedMa.containsMouse ? "#40ffffff" : "#22ffffff"
                        Text { anchors.centerIn: parent; text: (Math.round(mpv.speed * 100) / 100) + "x"; color: "white"; font.family: "Consolas"; font.bold: true; font.pixelSize: 13 }
                        MouseArea { id: speedMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: speedPanel.visible = !speedPanel.visible }
                    }

                    Rectangle {
                        width: 46; height: 32; radius: 7; color: subMa.containsMouse ? "#40ffffff" : "#22ffffff"
                        Text { anchors.centerIn: parent; text: "CC"; color: "white"; font.family: "Consolas"; font.bold: true; font.pixelSize: 13 }
                        MouseArea { id: subMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: subMenu.visible = !subMenu.visible }
                    }

                    Rectangle {
                        width: 36; height: 36; radius: 7; color: fsMa.containsMouse ? "#33ffffff" : "transparent"
                        Text { anchors.centerIn: parent; text: appWindow.visibility === Window.FullScreen ? "\u2196" : "\u26F6"; color: "white"; font.pixelSize: 15 }
                        MouseArea { id: fsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: playerRoot.toggleFullscreen() }
                    }
                }
            }
        }
    }
}