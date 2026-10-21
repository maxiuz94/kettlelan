import QtQuick
import QtQuick.Layouts
import KettleLAN

// Card with a value to copy (address, network code).
Rectangle {
    id: card
    property string label
    property string value
    property string copyMessage
    signal copyRequested()

    implicitHeight: col.implicitHeight + 24
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 12
        anchors.leftMargin: 14
        spacing: 4
        Text { textFormat: Text.PlainText;
            text: card.label
            color: Theme.muted
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: card.value
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            IconButton {
                implicitWidth: 34
                implicitHeight: 34
                iconName: "copy"
                iconSize: 16
                label: qsTr("Copy")
                onClicked: card.copyRequested()
            }
        }
    }
}
