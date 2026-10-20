import QtQuick
import QtQuick.Controls.Basic
import KettleLAN

// Przycisk z samą ikoną (min. 44×44 px). `label` trafia do podpowiedzi i czytnika ekranu.
Button {
    id: b
    property string iconName
    property string label
    property real iconSize: 20
    property color iconColor: Theme.muted

    implicitWidth: 44
    implicitHeight: 44
    hoverEnabled: true
    Accessible.name: label
    ToolTip.visible: hovered && label !== ""
    ToolTip.text: label
    ToolTip.delay: 500

    contentItem: Item {
        SvgIcon {
            anchors.centerIn: parent
            name: b.iconName
            size: b.iconSize
            color: b.hovered ? Theme.text : b.iconColor
        }
    }
    background: Rectangle {
        radius: Theme.smallRadius + 2
        color: b.down ? Theme.border : (b.hovered ? Theme.surface2 : "transparent")
        border.width: b.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
