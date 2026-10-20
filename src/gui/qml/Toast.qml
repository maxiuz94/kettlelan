import QtQuick
import KettleLAN

// Krótki komunikat u dołu okna (skopiowano, ktoś dołączył, błąd…).
Rectangle {
    id: toast
    property alias text: label.text

    function show(message) {
        label.text = message
        opacity = 1
        hideTimer.restart()
    }

    width: Math.min(parent ? parent.width - 40 : 360, label.implicitWidth + 32)
    height: label.implicitHeight + 22
    radius: Theme.radius
    color: Theme.dark ? "#E9ECF1" : "#15181D"
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    Text { textFormat: Text.PlainText;
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.parent ? toast.parent.width - 72 : 330)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.dark ? "#15181D" : "#E9ECF1"
        font.pixelSize: 13
    }
    Timer { id: hideTimer; interval: 3500; onTriggered: toast.opacity = 0 }
}
