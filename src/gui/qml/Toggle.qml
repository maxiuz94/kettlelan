import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN

// Wiersz ustawień z przełącznikiem.
Rectangle {
    id: row
    property string title
    property string hint
    property bool checked
    property bool last: false
    signal userToggled(bool value)

    implicitHeight: Math.max(56, texts.implicitHeight + 20)
    color: "transparent"
    opacity: enabled ? 1 : 0.5

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 12
        ColumnLayout {
            id: texts
            Layout.fillWidth: true
            spacing: 2
            Text { text: row.title; color: Theme.text; font.pixelSize: 14; font.weight: Font.Medium; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { text: row.hint; color: Theme.muted; font.pixelSize: 12; visible: row.hint !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        Switch {
            id: sw
            checked: row.checked
            Accessible.name: row.title
            onToggled: row.userToggled(checked)
            padding: 0
            implicitWidth: 42
            implicitHeight: 24
            indicator: Rectangle {
                x: 0
                y: (sw.height - height) / 2
                implicitWidth: 42
                implicitHeight: 24
                radius: 12
                color: sw.checked ? Theme.accent : Theme.surface2
                border.color: sw.checked ? Theme.accent : (sw.visualFocus ? Theme.accent : Theme.border)
                border.width: sw.visualFocus ? 2 : 1
                Rectangle {
                    x: sw.checked ? parent.width - width - 4 : 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    radius: 8
                    color: sw.checked ? Theme.accentInk : Theme.muted
                    Behavior on x { NumberAnimation { duration: 120 } }
                }
            }
            contentItem: Item {}
        }
    }
    Rectangle {
        visible: !row.last
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.border
    }
}
