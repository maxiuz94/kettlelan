import QtQuick
import QtQuick.Controls.Basic
import KettleLAN

// Button: "primary" (main action of a screen) or "secondary".
Button {
    id: b
    property string kind: "primary"
    property string iconName: ""

    implicitHeight: 48
    leftPadding: 18
    rightPadding: 18
    font.pixelSize: 15
    font.weight: Font.DemiBold
    hoverEnabled: true
    Accessible.name: text

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            SvgIcon {
                visible: b.iconName !== ""
                name: b.iconName
                size: 18
                strokeWidth: 2
                color: label.color
                anchors.verticalCenter: parent.verticalCenter
            }
            Text { textFormat: Text.PlainText;
                id: label
                text: b.text
                font: b.font
                color: b.kind === "primary" ? Theme.accentInk : Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        opacity: b.enabled ? 1 : 0.45
        color: b.kind === "primary"
               ? (b.down ? Theme.accentPressed : (b.hovered ? Theme.accentHover : Theme.accent))
               : (b.down ? Theme.border : (b.hovered ? Theme.surface2 : Theme.surface))
        border.width: b.kind === "primary" ? 0 : 1
        border.color: Theme.border

        Rectangle { // keyboard focus ring
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: b.visualFocus
        }
    }
}
