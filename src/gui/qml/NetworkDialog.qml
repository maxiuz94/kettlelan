import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Details of one network: the shared name (everyone sees it; only the person who opened the
// network can change it), your own note (only on this computer), the code, and "forget".
Popup {
    id: dialog
    property string code
    property string sharedName
    property string alias
    property bool connected: false
    property bool owner: false

    function openFor(code, sharedName, alias, connected, owner) {
        dialog.code = code
        dialog.sharedName = sharedName
        dialog.alias = alias
        dialog.connected = connected
        dialog.owner = owner
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 380, 400)
    modal: true
    focus: true
    padding: 20
    onOpened: { nameField.text = dialog.sharedName; aliasField.text = dialog.alias; aliasField.forceActiveFocus() }
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55000000" }

    contentItem: ColumnLayout {
        spacing: 14
        Text { textFormat: Text.PlainText; text: qsTr("Network details"); color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }

        // --- shared name
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { textFormat: Text.PlainText; text: qsTr("Network name"); color: Theme.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
            TextField {
                id: nameField
                visible: dialog.connected && dialog.owner
                Layout.fillWidth: true
                implicitHeight: 44
                color: Theme.text
                font.pixelSize: 15
                maximumLength: 60
                selectByMouse: true
                leftPadding: 12
                Accessible.name: qsTr("Network name")
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.bg
                    border.width: nameField.activeFocus ? 2 : 1
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                }
            }
            Text {
                visible: !(dialog.connected && dialog.owner)
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.sharedName !== "" ? dialog.sharedName : qsTr("Not known yet")
                color: dialog.sharedName !== "" ? Theme.text : Theme.muted
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 12
                text: dialog.connected && dialog.owner ? qsTr("Everyone in the network sees this name.")
                    : dialog.connected ? qsTr("Everyone sees the same name. Only the person who opened the network (★) can change it.")
                    : qsTr("Everyone in the network sees the same name.")
            }
        }

        // --- your own note
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { textFormat: Text.PlainText; text: qsTr("Your note (only on this computer)"); color: Theme.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
            TextField {
                id: aliasField
                Layout.fillWidth: true
                implicitHeight: 44
                placeholderText: qsTr("e.g. friends from work")
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.pixelSize: 15
                maximumLength: 60
                selectByMouse: true
                leftPadding: 12
                Accessible.name: qsTr("Your note")
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.bg
                    border.width: aliasField.activeFocus ? 2 : 1
                    border.color: aliasField.activeFocus ? Theme.accent : Theme.border
                }
                onAccepted: saveButton.clicked()
            }
        }

        // --- code
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ColumnLayout {
                spacing: 2
                Text { textFormat: Text.PlainText; text: qsTr("Network code"); color: Theme.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: dialog.code; color: Theme.text; font.family: Theme.mono; font.pixelSize: 15 }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                iconName: "copy"
                iconSize: 18
                label: qsTr("Copy network code")
                onClicked: Backend.copyText(dialog.code, qsTr("Network code copied – send it to your friends"))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            AppButton {
                visible: !dialog.connected
                kind: "secondary"
                text: qsTr("Forget")
                Accessible.name: qsTr("Forget network")
                onClicked: { Backend.forgetNetwork(dialog.code); dialog.close() }
            }
            Item { Layout.fillWidth: true }
            AppButton { kind: "secondary"; text: qsTr("Cancel"); onClicked: dialog.close() }
            AppButton {
                id: saveButton
                kind: "primary"
                text: qsTr("Save")
                onClicked: {
                    if (aliasField.text.trim() !== dialog.alias) Backend.setAlias(dialog.code, aliasField.text)
                    if (dialog.connected && dialog.owner && nameField.text.trim() !== "" && nameField.text.trim() !== dialog.sharedName)
                        Backend.renameNetwork(dialog.code, nameField.text)
                    dialog.close()
                }
            }
        }
    }
}
