import QtQuick


Item {
    id: root

    signal clicked()

    readonly property bool hovered: mouseArea.containsMouse

    implicitWidth: 40
    implicitHeight: 40

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.hovered ? "#2a2a2a" : "#1e1e1e"
        border.color: "#3a3a3a"
        border.width: 1

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    //  plus icon (2 lines )
    Item {
        anchors.centerIn: parent
        width: 18
        height: 18

        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: 2
            radius: 1
            color: "white"
        }
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: 18
            radius: 1
            color: "white"
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}