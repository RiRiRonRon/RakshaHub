import QtQuick
import QtQuick.Layouts



Rectangle {
    id: root

    property bool open: false
    signal movieSelected()
    signal showSelected()

    width: 180
    height: column.implicitHeight + 16
    radius: 10
    color: "#1c1c1c"
    border.color: "#333333"
    border.width: 1


    opacity: open ? 1 : 0
    visible: opacity > 0
    scale: open ? 1 : 0.85
    transformOrigin: Item.TopRight

    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }
    Behavior on scale {
        NumberAnimation { duration: 150; easing.type: Easing.OutBack }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        Rectangle {
            Layout.fillWidth: true
            height: 40
            radius: 6
            color: movieArea.containsMouse ? "#262626" : "transparent"

            Behavior on color {
                ColorAnimation { duration: 100 }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Movie"
                color: "white"
                font.family: "Consolas"
                font.bold: true
                font.italic: true
                font.pixelSize: 15
            }

            MouseArea {
                id: movieArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.movieSelected()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 40
            radius: 6
            color: showArea.containsMouse ? "#262626" : "transparent"

            Behavior on color {
                ColorAnimation { duration: 100 }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Show"
                color: "white"
                font.family: "Consolas"
                font.bold: true
                font.italic: true
                font.pixelSize: 15
            }

            MouseArea {
                id: showArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.showSelected()
            }
        }
    }
}
