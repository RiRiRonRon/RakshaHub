import QtQuick




Item{
    id:root
    signal clicked()
    readonly property bool hovered :mouseArea.containsMouse
    implicitWidth:40
    implicitHeight:40




    Rectangle{
        anchors.fill:parent
        radius:6
        color:root.hovered ?"#262626" : "transparent"
        Behavior on color {

            ColorAnimation {

                duration: 120
            }
        }
    }

    /// 3 bars MenuButton

    Column{
        anchors.centerIn:parent
        spacing:5
        Rectangle { width: 20; height: 2; radius: 1; color: "white" }
        Rectangle { width: 20; height: 2; radius: 1; color: "white" }
        Rectangle { width: 20; height: 2; radius: 1; color: "white" }

    }
    MouseArea{
        id:mouseArea
        anchors.fill:parent
        hoverEnabled:true
        cursorShape:Qt.PointingHandCursor
        onClicked:root.clicked()

    }



}
