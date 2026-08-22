import QtQuick
import QtQuick.Controls

Item {
    id: card

    property int    entryId:      -1
    property string showTitle:    ""
    property real   showRating:   0.0
    property string showKind:     "Show"
    property real   showProgress: 0.0
    property string showPosterUrl: ""
    property string showDuration: ""

    signal deleteRequested()
    signal showClicked()
    signal moveRequested(int fromIndex, int toIndex)


    readonly property bool dragging: GridView.view
        ? GridView.view.draggedIndex === index : false
   
    readonly property bool isDropTarget: GridView.view
        ? (GridView.view.draggedIndex !== -1
           && GridView.view.draggedIndex !== index
           && GridView.view.hoverIndex  === index)
        : false

    width: 185
    height: 295

    scale:   dragging ? 0.93 : (mouseArea.containsMouse ? 1.05 : 1.0)
    z:       dragging ? 10   : (mouseArea.containsMouse ? 2    : 0)
    opacity: dragging ? 0.45 : 1.0

    Behavior on scale   { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }
    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }

    // Poster box
    Rectangle {
        id: poster
        anchors.left:  parent.left
        anchors.right: parent.right
        anchors.top:   parent.top
        height: parent.height - 52
        radius: 8
        color: "#2a2a2a"
        border.color: card.isDropTarget ? "#4fc3f7"
                    : (mouseArea.containsMouse ? "#4fc3f7" : "transparent")
        border.width: card.isDropTarget ? 3 : 2
        clip: true

        Behavior on border.color { ColorAnimation { duration: 160 } }

        //  when no poster
        Text {
            visible: card.showPosterUrl.length === 0
            anchors.centerIn: parent
            text: card.showTitle.length > 0 ? card.showTitle[0] : "?"
            color: "#666"
            font.pixelSize: 52
            font.weight: Font.Medium
        }

        Image {
            anchors.fill: parent
            visible: card.showPosterUrl.length > 0
            source: card.showPosterUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true

            sourceSize.width:  185
            sourceSize.height: 243
            cache: true
        }

        // Ratings
        Rectangle {
            anchors.top:     parent.top
            anchors.right:   parent.right
            anchors.margins: 8
            width: 44; height: 22; radius: 4
            color: "#f5c518"
            Text {
                anchors.centerIn: parent
                text: card.showRating > 0 ? card.showRating.toFixed(1) : "—"
                color: "#000"; font.pixelSize: 12; font.weight: Font.Bold
            }
        }

        // Progress bar
        Item {
            visible: card.showProgress > 0 && card.showProgress < 1.0
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 18
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 6; anchors.rightMargin: 6
                anchors.bottomMargin: 6
                height: 4; radius: 2
                color: "#33ffffff"
                Rectangle {
                    width: parent.width * card.showProgress
                    height: parent.height; radius: 2; color: "#4fc3f7"
                }
            }
        }
    }

    // Title
    Text {
        anchors.top: poster.bottom; anchors.topMargin: 7
        anchors.left: parent.left;  anchors.right: parent.right
        text: card.showTitle; color: "white"
        font.family: "Consolas"; font.bold: true; font.italic: true
        font.pixelSize: 14; elide: Text.ElideRight; maximumLineCount: 1
    }

    //  type +and how long
    Text {
        anchors.top: poster.bottom; anchors.topMargin: 28
        anchors.left: parent.left
        text: card.showDuration.length > 0
              ? card.showKind + " · " + card.showDuration
              : card.showKind
        color: "#888"; font.family: "Consolas"; font.italic: true
        font.pixelSize: 12
    }

    // reorder cards
    MouseArea {
        id: mouseArea
        anchors.fill: poster
        hoverEnabled: true
        preventStealing: true
        pressAndHoldInterval: 350
        cursorShape: card.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor

        property bool isLongPress: false

        onPressAndHold: {
            isLongPress = true
            card.GridView.view.draggedIndex = index
            card.GridView.view.hoverIndex   = index
        }

        onPositionChanged: function(mouse) {
            if (!isLongPress) return
            const pos = mapToItem(card.GridView.view.contentItem, mouse.x, mouse.y)
            const i   = card.GridView.view.indexAt(pos.x, pos.y)
            if (i >= 0) card.GridView.view.hoverIndex = i
        }

        onReleased: function(mouse) {
            if (isLongPress) {
                const target = card.GridView.view.hoverIndex
                card.GridView.view.draggedIndex = -1
                card.GridView.view.hoverIndex   = -1
                isLongPress = false
                if (target >= 0 && target !== index)
                    card.moveRequested(index, target)
            } else if (mouse.x >= 0 && mouse.x <= width
                    && mouse.y >= 0 && mouse.y <= height) {
                card.showClicked()
            }
        }

        onCanceled: {
            card.GridView.view.draggedIndex = -1
            card.GridView.view.hoverIndex   = -1
            isLongPress = false
        }
    }

    // Delete card button
    Rectangle {
        id: deleteButton
        visible: mouseArea.containsMouse || deleteMouseArea.containsMouse
        anchors.top: poster.top; anchors.left: poster.left
        anchors.margins: 8
        width: 24; height: 24; radius: 12
        color: deleteMouseArea.containsMouse ? "#e05252" : "#00000099"
        Behavior on color { ColorAnimation { duration: 120 } }

        Text {
            anchors.centerIn: parent
            text: "\u2715"; color: "white"
            font.pixelSize: 12; font.bold: true
        }

        MouseArea {
            id: deleteMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.deleteRequested()
        }
    }
}