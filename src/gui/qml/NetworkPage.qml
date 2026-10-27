import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Active network screen: address, code and member list.
Item {
    id: page
    signal openSettings()


    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 6
            IconButton { iconName: "back"; label: qsTr("Back to your networks"); onClicked: Backend.closeNetwork() }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    textFormat: Text.PlainText
                    text: Backend.networkName !== "" ? Backend.networkName : qsTr("Unnamed network")
                    color: Backend.networkName !== "" ? Theme.text : Theme.muted
                    font.italic: Backend.networkName === ""
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text { textFormat: Text.PlainText; visible: Backend.networkAlias !== ""; text: Backend.networkAlias; color: Theme.accentText; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                RowLayout {
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4; color: Theme.ok }
                    Text { textFormat: Text.PlainText; text: (Backend.memberCount === 1 ? qsTr("Connected · 1 member") : qsTr("Connected · %1 members").arg(Backend.memberCount)); color: Theme.muted; font.pixelSize: 13 }
                }
            }
            IconButton {
                iconName: "edit"
                label: qsTr("Network details")
                onClicked: details.openFor(Backend.networkCode, Backend.networkName, Backend.networkAlias, true, Backend.networkOwner)
            }
            IconButton { iconName: "settings"; label: qsTr("Settings"); onClicked: page.openSettings() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.pad
            spacing: 16

            GridLayout {
                Layout.fillWidth: true
                columns: page.width < 560 ? 1 : 2
                columnSpacing: 10
                rowSpacing: 10
                InfoCard {
                    Layout.fillWidth: true
                    label: qsTr("Your address")
                    value: Backend.selfAddress
                    onCopyRequested: Backend.copyText(Backend.selfAddress, qsTr("Your address was copied"))
                }
                InfoCard {
                    Layout.fillWidth: true
                    label: qsTr("Network code")
                    value: Backend.networkCode
                    onCopyRequested: Backend.copyText(Backend.networkCode, qsTr("Network code copied – send it to your friends"))
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; text: qsTr("Members"); color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Text { textFormat: Text.PlainText; text: qsTr("Click a person to copy their address"); color: Theme.muted; font.pixelSize: 12 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                clip: true

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 1
                    model: Backend.peers
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Button {
                        id: row
                        required property var modelData
                        required property int index
                        width: list.width
                        implicitHeight: 62
                        hoverEnabled: true
                        enabled: true
                        Accessible.name: modelData.name + ", " + modelData.address

                        readonly property string badgeText: modelData.self ? qsTr("You")
                            : modelData.blocked ? qsTr("Blocked")
                            : modelData.conflict ? qsTr("Address conflict")
                            : !modelData.verified ? qsTr("Connecting…")
                            : modelData.pingMs < 0 ? qsTr("Connected")
                            : modelData.relayed ? qsTr("Steam relay") : qsTr("Direct")
                        readonly property color badgeBg: (modelData.self || modelData.blocked || !modelData.verified || modelData.pingMs < 0) ? Theme.surface2
                            : (modelData.relayed || modelData.conflict) ? Theme.relayBg : Theme.directBg
                        readonly property color badgeFg: (modelData.self || modelData.blocked || !modelData.verified || modelData.pingMs < 0) ? Theme.muted
                            : (modelData.relayed || modelData.conflict) ? Theme.relayFg : Theme.directFg

                        onClicked: if (!modelData.self && modelData.verified) {
                                       Backend.copyText(modelData.address, "")
                                       rowCopied.restart()
                                   }
                        Timer { id: rowCopied; interval: 1500 }

                        background: Rectangle {
                            color: row.down ? Theme.border : (row.hovered && !row.modelData.self ? Theme.surface2 : "transparent")
                            Rectangle {
                                visible: row.index < list.count - 1
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 1
                                color: Theme.border
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                width: 36; height: 36; radius: 18
                                color: Theme.surface2
                                Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: row.modelData.initial; color: Theme.text; font.pixelSize: 14; font.weight: Font.Bold }
                            }
                            ColumnLayout {
                                id: nameColumn
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    id: nameRow
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { textFormat: Text.PlainText;
                                        // as wide as the name, but leave room for the badges
                                        Layout.preferredWidth: Math.min(implicitWidth, nameColumn.width
                                            - (friendBadge.visible ? friendBadge.implicitWidth + 6 : 0)
                                            - (sameIcon.visible ? 20 : 0))
                                        text: row.modelData.name + (row.modelData.owner ? "  ★" : "")
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    // your friend on Steam (checked on this computer); their Steam name on hover
                                    Rectangle {
                                        id: friendBadge
                                        visible: row.modelData.steamFriend === true
                                        implicitWidth: friendText.implicitWidth + 10
                                        implicitHeight: 18
                                        radius: 9
                                        color: Theme.directBg
                                        Text { id: friendText; textFormat: Text.PlainText; anchors.centerIn: parent; text: qsTr("Steam friend"); color: Theme.directFg; font.pixelSize: 10; font.weight: Font.DemiBold }
                                        HoverHandler { id: friendHover }
                                        ToolTip.visible: friendHover.hovered
                                        ToolTip.text: qsTr("On Steam: %1").arg(row.modelData.steamName)
                                        ToolTip.delay: 300
                                    }
                                    // two members with the same name: check the address
                                    SvgIcon {
                                        id: sameIcon
                                        visible: row.modelData.sameName === true
                                        name: "alert"
                                        size: 14
                                        color: Theme.relayFg
                                        HoverHandler { id: sameHover }
                                        ToolTip.visible: sameHover.hovered
                                        ToolTip.text: qsTr("Same name as another member – check the address")
                                        ToolTip.delay: 300
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                Text { textFormat: Text.PlainText; text: rowCopied.running ? qsTr("Copied ✓") : row.modelData.address; color: rowCopied.running ? Theme.ok : Theme.muted; font.family: Theme.mono; font.pixelSize: 12 }
                            }
                            ColumnLayout {
                                spacing: 4
                                Text { textFormat: Text.PlainText;
                                    Layout.alignment: Qt.AlignRight
                                    text: row.modelData.pingMs >= 0 ? row.modelData.pingMs + " ms" : "—"
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignRight
                                    implicitWidth: badge.implicitWidth + 16
                                    implicitHeight: badge.implicitHeight + 6
                                    radius: height / 2
                                    color: row.badgeBg
                                    Text { textFormat: Text.PlainText; id: badge; anchors.centerIn: parent; text: row.badgeText; color: row.badgeFg; font.pixelSize: 11; font.weight: Font.DemiBold }
                                }
                            }
                            // block / unblock – faint until the row is hovered; never for ourselves
                            IconButton {
                                // for ourselves an empty space of the same width, so that all rows line up
                                enabled: !row.modelData.self
                                opacity: row.modelData.self ? 0 : (row.hovered || hovered || visualFocus || row.modelData.blocked ? 1 : 0.55)
                                iconName: "block"
                                iconSize: 18
                                iconColor: row.modelData.blocked ? Theme.accentText : Theme.muted
                                label: row.modelData.blocked ? qsTr("Unblock %1").arg(row.modelData.name) : qsTr("Block %1").arg(row.modelData.name)
                                onClicked: row.modelData.blocked ? Backend.unblockPerson(row.modelData.steamId)
                                                                 : blockDialog.openFor(row.modelData.steamId, row.modelData.name)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AppButton {
                    Layout.fillWidth: true
                    kind: "primary"
                    iconName: "userPlus"
                    text: inviteCopied.running ? qsTr("Code copied – send it to your friends") : qsTr("Invite friends")
                    onClicked: { Backend.copyText(Backend.networkCode, ""); inviteCopied.restart() }
                    Timer { id: inviteCopied; interval: 2500 }
                }
                AppButton {
                    kind: "secondary"
                    iconName: "leave"
                    text: qsTr("Disconnect")
                    onClicked: Backend.leaveNetwork()
                }
            }
        }
    }

    NetworkDialog { id: details }

    // Blocking someone asks once: it cuts them off in every network until you unblock them.
    Popup {
        id: blockDialog
        property string steamId
        property string name
        function openFor(steamId, name) { blockDialog.steamId = steamId; blockDialog.name = name; open() }

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 40 : 380, 400)
        modal: true
        focus: true
        padding: 20
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.border }
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55000000" }

        contentItem: ColumnLayout {
            spacing: 14
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap
                text: qsTr("Block %1?").arg(blockDialog.name); color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 13
                text: qsTr("Nothing from them reaches your computer, and they do not see you or your game servers – in every network, until you unblock them. They are not told. You can unblock them here or in Settings.") }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                Item { Layout.fillWidth: true }
                AppButton { kind: "secondary"; text: qsTr("Cancel"); onClicked: blockDialog.close() }
                AppButton { kind: "primary"; text: qsTr("Block"); onClicked: { Backend.blockPerson(blockDialog.steamId, blockDialog.name); blockDialog.close() } }
            }
        }
    }
    // demo mode screenshot of the dialog
    Component.onCompleted: if (Backend.demoPage === "details")
        Qt.callLater(() => details.openFor(Backend.networkCode, Backend.networkName, "my Quake friends", true, Backend.networkOwner))
}
