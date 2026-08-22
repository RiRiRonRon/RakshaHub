import QtQuick
import QtQuick.Layouts


Rectangle{
    id:root
    property string label: ""
    property bool active: false

    signal clicked()
    Layout.fillWidth: true
    height: 48
    color: active ? "#2a2a2a" : (mouseArea.containsMouse ? "#222222" : "transparent")
    Behavior on color{

        ColorAnimation {

            duration: 120
        }
    }
    Rectangle{
        visible: root.active
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: "#4fc3f7"
    }
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: root.active ? "white" : "#cccccc"
        font.family: "Consolas"
        font.bold: true
        font.italic: true
        font.pixelSize: 18
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }






}
