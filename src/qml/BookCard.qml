import QtQuick
import QtQuick.Controls
import Raksha_Hub

Item {
    id: card
    property int    bookId:      -1
    property string bookTitle:   ""
    property string bookAuthor:  ""
    property string bookFormat:  ""
    property string coverPath:   ""
    property real   bookProgress: 0.0

    signal deleteRequested()
    signal bookClicked()

    width: 185
    height: 295

    scale: mouseArea.containsMouse ? 1.05 : 1.0
    z:     mouseArea.containsMouse ? 2 : 0
    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }

    // Poster
    Rectangle {
        id: poster
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.top:    parent.top
        height: parent.height - 52
        radius: 8
        color: "#2a2a2a"
        border.color: mouseArea.containsMouse ? "#4fc3f7" : "transparent"
        border.width: 2
        clip: true
        Behavior on border.color { ColorAnimation { duration: 160 } }

        // Cover image
        Image {
            id: coverImg
            anchors.fill: parent
            visible: card.coverPath.length > 0 && status === Image.Ready
            source: card.coverPath
            fillMode: Image.PreserveAspectCrop
            asynchronous: true

            //RAM Optimizations
            cache: false               // Releases image texture memory immediately when scrolled off-screen
            sourceSize.width: 185      // Restricts texture memory allocation to exact card display width
            sourceSize.height: 243     // Restricts texture memory allocation to exact card display height

            onStatusChanged: {
                if (status === Image.Error && card.coverPath.length > 0) {
                    console.log("Cover image failed to load for:", card.bookTitle, "Path:", card.coverPath)
                }
            }
        }

        // Fallback  (first letter) if no cover
        Text {
            visible: card.coverPath.length === 0 || coverImg.status !== Image.Ready
            anchors.centerIn: parent
            text: card.bookTitle.length > 0 ? card.bookTitle[0].toUpperCase() : "?"
            color: "#666"
            font.pixelSize: 52
            font.weight: Font.Medium
        }

        // Format badge (pdf,epub,...)
        Rectangle {
            anchors.top:     parent.top
            anchors.right:   parent.right
            anchors.margins: 8
            width:  fmtLabel.implicitWidth + 10
            height: 22; radius: 4
            color: card.bookFormat === "PDF"  ? "#e05252"
                 : card.bookFormat === "EPUB" ? "#4a7fc1"
                 :                              "#e8a020"
            Text {
                id: fmtLabel
                anchors.centerIn: parent
                text:  card.bookFormat
                color: "white"
                font.pixelSize: 11
                font.weight: Font.Bold
                font.family: "Consolas"
            }
        }

        // Progress bar
        Item {
            visible: card.bookProgress > 0 && card.bookProgress < 1.0
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            height: 18
            Rectangle {
                anchors.left:         parent.left
                anchors.right:        parent.right
                anchors.bottom:       parent.bottom
                anchors.leftMargin:   6
                anchors.rightMargin:  6
                anchors.bottomMargin: 6
                height: 4; radius: 2
                color: "#33ffffff"
                Rectangle {
                    width: parent.width * card.bookProgress
                    height: parent.height; radius: 2; color: "#4fc3f7"
                }
            }
        }
    }

    //  Title
    Text {
        anchors.top:       poster.bottom
        anchors.topMargin: 7
        anchors.left:      parent.left
        anchors.right:     parent.right
        text:  card.bookTitle
        color: "white"
        font.family: "Consolas"; font.bold: true; font.italic: true
        font.pixelSize: 13; elide: Text.ElideRight; maximumLineCount: 1
    }

    // Author & Format
    Text {
        anchors.top:       poster.bottom
        anchors.topMargin: 27
        anchors.left:      parent.left
        anchors.right:     progressText.visible ? progressText.left : parent.right
        anchors.rightMargin: progressText.visible ? 6 : 0
        text:  card.bookAuthor.length > 0 ? card.bookAuthor : card.bookFormat
        color: "#888"
        font.family: "Consolas"; font.italic: true
        font.pixelSize: 12; elide: Text.ElideRight
    }

    // Progress Percentage
    Text {
        id: progressText
        visible: card.bookProgress > 0.0
        anchors.top:       poster.bottom
        anchors.topMargin: 27
        anchors.right:     parent.right
        text: Math.round(card.bookProgress * 100) + "%"
        color: "#4fc3f7"
        font.family: "Consolas"; font.bold: true; font.italic: true
        font.pixelSize: 12
    }

    // Click
    MouseArea {
        id: mouseArea
        anchors.fill: poster
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    card.bookClicked()
    }

    // Delete button
    Rectangle {
        visible: mouseArea.containsMouse || delMa.containsMouse
        anchors.top:    poster.top
        anchors.left:   poster.left
        anchors.margins: 8
        width: 24; height: 24; radius: 12
        color: delMa.containsMouse ? "#e05252" : "#00000099"
        Behavior on color { ColorAnimation { duration: 120 } }
        Text {
            anchors.centerIn: parent
            text: "\u2715"; color: "white"
            font.pixelSize: 12; font.bold: true
        }
        MouseArea {
            id: delMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.deleteRequested()
        }
    }
}