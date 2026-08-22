import QtQuick



Rectangle {
    id: toast

    property string message: ""

    function show(text) {
        message = text
        opacity = 1
        hideTimer.restart()
    }

    width: toastText.implicitWidth + 32
    height: 40
    radius: 8
    color: "#1c1c1c"
    border.color: "#333333"
    border.width: 1

    opacity: 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: 200 }
    }

    Text {
        id: toastText
        anchors.centerIn: parent
        text: toast.message
        color: "white"
        font.family: "Consolas"
        font.pixelSize: 13
    }


    Timer {
        id: hideTimer
        interval: 2200
        onTriggered: toast.opacity = 0
    }
}
