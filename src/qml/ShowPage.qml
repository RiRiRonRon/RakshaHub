import QtQuick
import QtQuick.Layouts
import Raksha_Hub

Item {
    id: root

    property int    entryId:    -1
    property string showTitle:  ""
    property real   showRating: 0.0
    property string posterUrl:  ""
    property string showKind:   "Show"

    signal backRequested()
    signal playRequested(string filePath, real startMs, int entryId,
                         int season, int episode, string episodeTitle)

    property var allEpisodes: []
    property int activeSeason: 1


    ListModel { id: episodeModel }


    function rebuildModel() {
        episodeModel.clear()
        for (var i = 0; i < allEpisodes.length; i++) {
            var ep = allEpisodes[i]
            if (ep.season !== activeSeason) continue
            episodeModel.append({
                "filePath":      ep.filePath      || "",
                "title":         ep.title         || "",
                "episode":       ep.episode       || 0,
                "season":        ep.season        || 1,
                "duration":      ep.duration      || "",
                "durationMs":    ep.durationMs    || 0,
                "positionMs":    ep.positionMs    || 0,
                "rating":        ep.rating        || "",
                "thumbnailPath": ep.thumbnailPath || ""
            })
        }
    }


    function patchThumbnails() {
        var fresh = LibraryManager.episodesForShow(root.entryId)

        // Build lookup: episode number → thumbnailPath (for current season)
        var thumbMap = {}
        for (var i = 0; i < fresh.length; i++) {
            if (fresh[i].season === activeSeason)
                thumbMap[fresh[i].episode] = fresh[i].thumbnailPath || ""
        }

        // Store internally without triggering rebuild
        allEpisodes = fresh

        // Patch only changed thumbnails — no clear(), no rebuild
        for (var j = 0; j < episodeModel.count; j++) {
            var epNum = episodeModel.get(j).episode
            var newThumb = thumbMap[epNum] || ""
            if (episodeModel.get(j).thumbnailPath !== newThumb)
                episodeModel.setProperty(j, "thumbnailPath", newThumb)
        }
    }

    onActiveSeasonChanged: rebuildModel()

    onEntryIdChanged: {
        if (entryId >= 0) {
            allEpisodes = LibraryManager.episodesForShow(entryId)

            if (continueEpisode)
                activeSeason = continueEpisode.season
            else if (seasonNumbers.length > 0)
                activeSeason = seasonNumbers[0]

            rebuildModel()
            LibraryManager.rescanShow(entryId)
        }
    }

    property var seasonNumbers: {
        var seen = {}
        var arr  = []
        for (var i = 0; i < allEpisodes.length; i++) {
            var s = allEpisodes[i].season
            if (!seen[s]) { seen[s] = true; arr.push(s) }
        }
        arr.sort(function(a, b) { return a - b })
        return arr
    }

    property var continueEpisode: {
        var lastKey = LibraryManager.getSetting("last_ep_" + root.entryId, "")
        if (lastKey.length > 0) {
            var parts = lastKey.split(",")
            var lastS = parseInt(parts[0])
            var lastE = parseInt(parts[1])
            for (var i = 0; i < allEpisodes.length; i++) {
                if (allEpisodes[i].season === lastS &&
                    allEpisodes[i].episode === lastE)
                    return allEpisodes[i]
            }
        }
        for (var j = 0; j < allEpisodes.length; j++) {
            var ep  = allEpisodes[j]
            var pct = ep.durationMs > 0 ? ep.positionMs / ep.durationMs : 0
            if (pct > 0 && pct < 0.97) return ep
        }
        return allEpisodes.length > 0 ? allEpisodes[0] : null
    }

    // Throttle thumbnail patches: coalesce burst into one patch 400ms after last signal
    Timer {
        id: thumbThrottle
        interval: 400
        repeat: false
        onTriggered: root.patchThumbnails()
    }

    Connections {
        target: LibraryManager
        function onEpisodesUpdated(updatedId) {
            if (updatedId !== root.entryId) return
            thumbThrottle.restart()
        }
    }

    Rectangle { anchors.fill: parent; color: "#111111" }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        spacing: 10
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3000
        maximumFlickVelocity: 5000
        highlightMoveDuration: 0
        cacheBuffer: 1000
        reuseItems: true

        model: episodeModel

        // Header (Poster + Show Info + Season Tabs)
        header: ColumnLayout {
            width: listView.width
            spacing: 0

            Item {
                Layout.fillWidth: true
                height: 62

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    spacing: 10

                    Rectangle {
                        width: 38; height: 38; radius: 19
                        color: backMa.containsMouse ? "#2a2a2a" : "#1e1e1e"
                        border.color: "#2e2e2e"; border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Text { anchors.centerIn: parent; text: "\u2190"; color: "#aaa"; font.pixelSize: 18 }
                        MouseArea {
                            id: backMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.backRequested()
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "My Library"; color: "#555"
                        font.family: "Consolas"; font.pixelSize: 15
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: heroRow.implicitHeight + 44
                Layout.leftMargin: 32; Layout.rightMargin: 32

                Row {
                    id: heroRow
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.topMargin: 8
                    spacing: 30

                    Rectangle {
                        width: 175; height: 260; radius: 10; color: "#1e1e1e"
                        border.color: "#2a2a2a"; border.width: 1; clip: true

                        Text {
                            visible: root.posterUrl.length === 0
                            anchors.centerIn: parent
                            text: root.showTitle.length > 0 ? root.showTitle[0] : "?"
                            color: "#444"; font.pixelSize: 66
                        }

                        Image {
                            anchors.fill: parent
                            visible: root.posterUrl.length > 0
                            source: root.posterUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 350
                        }
                    }

                    ColumnLayout {
                        width: parent.width - 205
                        spacing: 0

                        Text {
                            text: root.showTitle; color: "#f0f0f0"
                            font.family: "Consolas"; font.bold: true
                            font.pixelSize: 34; elide: Text.ElideRight
                            Layout.fillWidth: true; Layout.topMargin: 6
                        }

                        Row {
                            spacing: 8; Layout.topMargin: 16
                            Repeater {
                                model: {
                                    var pills = []
                                    if (root.seasonNumbers.length > 0)
                                        pills.push(root.seasonNumbers.length + " season" + (root.seasonNumbers.length > 1 ? "s" : ""))
                                    if (root.allEpisodes.length > 0)
                                        pills.push(root.allEpisodes.length + " episodes")
                                    return pills
                                }
                                delegate: Rectangle {
                                    height: 28; radius: 14; color: "#1a1a1a"; border.color: "#2e2e2e"; border.width: 1
                                    width: pillText.implicitWidth + 24
                                    Text { id: pillText; anchors.centerIn: parent; text: modelData; color: "#888"; font.family: "Consolas"; font.pixelSize: 13 }
                                }
                            }

                            Rectangle {
                                visible: root.showRating > 0
                                height: 28; radius: 14; color: "#1a1a1a"; border.color: "#2e2e2e"; border.width: 1
                                width: ratingRow.implicitWidth + 24
                                Row {
                                    id: ratingRow; anchors.centerIn: parent; spacing: 5
                                    Text { text: "\u2605"; color: "#e8c05a"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: root.showRating.toFixed(1); color: "#e8c05a"; font.family: "Consolas"; font.bold: true; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                        }

                        Rectangle {
                            Layout.topMargin: 24; width: 225
                            height: root.continueEpisode && root.continueEpisode.positionMs > 0 ? 58 : 48
                            radius: 9; clip: true

                            Rectangle {
                                anchors.fill: parent; radius: parent.radius
                                color: continueMa.containsMouse ? "#1e2a3a" : "#161e2a"
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; radius: parent.radius
                                width: {
                                    if (!root.continueEpisode || root.continueEpisode.positionMs <= 0) return parent.width * 0.08
                                    var pct = root.continueEpisode.durationMs > 0 ? root.continueEpisode.positionMs / root.continueEpisode.durationMs : 0
                                    return parent.width * Math.max(0.08, Math.min(pct, 0.96))
                                }
                                color: "#4a7fc1"
                            }
                            Row {
                                anchors.centerIn: parent; spacing: 10
                                Text { anchors.verticalCenter: parent.verticalCenter; text: "\u25B6"; color: "white"; font.pixelSize: 14 }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                    Text {
                                        text: {
                                            if (!root.continueEpisode) return "Play"
                                            if (root.continueEpisode.positionMs <= 0) return "Start Watching"
                                            return "Continue Watching"
                                        }
                                        color: "white"; font.family: "Consolas"; font.bold: true; font.pixelSize: 15
                                    }
                                    Text {
                                        visible: root.continueEpisode !== null && root.continueEpisode.positionMs > 0
                                        text: root.continueEpisode ? "S" + root.continueEpisode.season + " · E" + root.continueEpisode.episode : ""
                                        color: "#a0c4e8"; font.family: "Consolas"; font.pixelSize: 12
                                    }
                                }
                            }
                            MouseArea {
                                id: continueMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!root.continueEpisode) return
                                    var ep = root.continueEpisode
                                    root.playRequested(ep.filePath, ep.positionMs, root.entryId, ep.season, ep.episode, ep.title || "")
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32
                Layout.topMargin: 18; Layout.bottomMargin: 26; height: 1; color: "#1e1e1e"
            }

            Item {
                Layout.fillWidth: true; Layout.leftMargin: 32
                implicitHeight: seasonRow.implicitHeight

                Row {
                    id: seasonRow; spacing: 8
                    Repeater {
                        model: root.seasonNumbers
                        delegate: Rectangle {
                            width: tabLabel.implicitWidth + 36; height: 38; radius: 19
                            color: root.activeSeason === modelData ? "#e8e8e8" : (tabMa.containsMouse ? "#1e1e1e" : "#161616")
                            border.color: root.activeSeason === modelData ? "transparent" : "#2a2a2a"; border.width: 1
                            Text {
                                id: tabLabel; anchors.centerIn: parent; text: "Season " + modelData
                                color: root.activeSeason === modelData ? "#111" : "#666"
                                font.family: "Consolas"; font.bold: root.activeSeason === modelData; font.pixelSize: 14
                            }
                            MouseArea {
                                id: tabMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.activeSeason = modelData
                            }
                        }
                    }
                }
            }

            Item { height: 18 }
        }

        // Episode tab
        delegate: Item {
            width: listView.width
            height: 122

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 32; anchors.rightMargin: 32
                radius: 10
                color: epMa.containsMouse ? "#1c1c1c" : "#161616"
                border.color: {
                    var pct = durationMs > 0 ? positionMs / durationMs : 0
                    return (pct > 0 && pct < 0.97) ? "#3a5a8a" : "#222222"
                }
                border.width: 1

                Row {
                    anchors.fill: parent; anchors.margins: 13; spacing: 14

                    Rectangle {
                        width: 152; height: 86; anchors.verticalCenter: parent.verticalCenter
                        radius: 6; color: "#1e1e1e"; border.color: "#2a2a2a"; border.width: 1; clip: true

                        Image {
                            id: thumbImg
                            anchors.fill: parent
                            visible: thumbnailPath.length > 0
                            source: thumbnailPath.length > 0
                                    ? "file:///" + thumbnailPath.replace(/\\/g, "/")
                                    : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            sourceSize.width: 152
                        }

                        Text {
                            anchors.centerIn: parent; text: "\u25B6"
                            color: thumbnailPath.length > 0 ? "#ffffffaa" : (epMa.containsMouse ? "#777" : "#2e2e2e")
                            font.pixelSize: 26
                        }
                    }

                    ColumnLayout {
                        width: parent.width - 152 - 14 - 86 - 14
                        anchors.verticalCenter: parent.verticalCenter; spacing: 5

                        Row {
                            spacing: 9
                            Text { text: "E" + String(episode).padStart(2, "0"); color: "#555"; font.family: "Consolas"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: title || ("Episode " + episode); color: "#d8d8d8"; font.family: "Consolas"; font.bold: true; font.pixelSize: 17; elide: Text.ElideRight; width: parent.parent.width - 48 }
                        }

                        Row {
                            spacing: 6
                            Text { text: duration || ""; color: "#555"; font.family: "Consolas"; font.pixelSize: 14; visible: (duration || "").length > 0 }
                            Text { text: "\u00B7"; color: "#444"; font.pixelSize: 14; visible: (duration || "").length > 0 && (rating || "").length > 0 && rating !== "N/A" }
                            Text { text: "\u2605 " + (rating || ""); color: "#e8c05a"; font.family: "Consolas"; font.pixelSize: 14; visible: (rating || "").length > 0 && rating !== "N/A" }
                        }

                        Rectangle {
                            width: parent.width; height: 2; radius: 1; color: "#222"; Layout.topMargin: 2
                            Rectangle {
                                width: durationMs > 0 ? parent.width * Math.min(1.0, positionMs / durationMs) : 0
                                height: 2; radius: 1; color: "#4a7fc1"
                            }
                        }
                    }

                    Text {
                        width: 86; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight
                        text: {
                            if (durationMs <= 0) return duration || ""
                            var pct = positionMs / durationMs
                            if (pct <= 0) return duration || ""
                            var leftMs  = durationMs - positionMs
                            var leftMin = Math.round(leftMs / 60000)
                            var doneMin = Math.round(positionMs / 60000)
                            if (leftMin <= 3) return duration || ""
                            return pct > 0.5 ? leftMin + " min left" : doneMin + " min in"
                        }
                        color: "#555"; font.family: "Consolas"; font.pixelSize: 14
                    }
                }

                MouseArea {
                    id: epMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.playRequested(filePath, positionMs, root.entryId, season, episode, title || "")
                    }
                }
            }
        }

        footer: Item {
            width: listView.width; height: 80
            visible: episodeModel.count === 0
            Text {
                anchors.centerIn: parent
                text: "No episodes found for this season"
                color: "#444"; font.family: "Consolas"; font.italic: true; font.pixelSize: 15
            }
        }
    }

    // Scrollbar
    Item {
        id: scrollBar
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.rightMargin: 4; width: 14
        visible: listView.contentHeight > listView.height

        readonly property real trackTop:    12
        readonly property real trackH:      height - 24
        readonly property real thumbH:      Math.max(32, (listView.height / Math.max(1, listView.contentHeight)) * trackH)
        readonly property real maxContentY: Math.max(1, listView.contentHeight - listView.height)

        Rectangle {
            anchors.centerIn: parent; width: 3; height: scrollBar.trackH; radius: 2; color: "#1e1e1e"
        }

        Rectangle {
            id: thumb
            anchors.horizontalCenter: parent.horizontalCenter
            width:  thumbMa.containsMouse || thumbMa.pressed ? 6 : 3
            height: scrollBar.thumbH
            radius: width / 2
            color:  thumbMa.pressed ? "#cccccc" : (thumbMa.containsMouse ? "#888888" : "#444444")
            Behavior on width { NumberAnimation { duration: 120 } }
            Behavior on color { ColorAnimation  { duration: 120 } }
            y: scrollBar.trackTop + (listView.contentY / scrollBar.maxContentY) * (scrollBar.trackH - scrollBar.thumbH)

            MouseArea {
                id: thumbMa; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor; preventStealing: true
                property real grabOffsetY: 0
                onPressed:        function(m) { grabOffsetY = thumb.y + m.y }
                onPositionChanged: function(m) {
                    if (!pressed) return
                    var cur   = thumb.y + m.y
                    var delta = cur - grabOffsetY
                    grabOffsetY = cur
                    var ratio = delta / (scrollBar.trackH - scrollBar.thumbH)
                    var newY  = listView.contentY + ratio * scrollBar.maxContentY
                    listView.contentY = Math.max(0, Math.min(newY, scrollBar.maxContentY))
                }
            }
        }
    }
}