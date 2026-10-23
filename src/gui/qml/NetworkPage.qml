import QtQuick
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
                Text { textFormat: Text.PlainText; text: Backend.networkName; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold; elide: Text.ElideRight; Layout.fillWidth: true }
                RowLayout {
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4; color: Theme.ok }
                    Text { textFormat: Text.PlainText; text: (Backend.memberCount === 1 ? qsTr("Connected · 1 member") : qsTr("Connected · %1 members").arg(Backend.memberCount)); color: Theme.muted; font.pixelSize: 13 }
                }
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
                            : modelData.conflict ? qsTr("Address conflict")
                            : !modelData.verified ? qsTr("Connecting…")
                            : modelData.pingMs < 0 ? qsTr("Connected")
                            : modelData.relayed ? qsTr("Steam relay") : qsTr("Direct")
                        readonly property color badgeBg: (modelData.self || !modelData.verified || modelData.pingMs < 0) ? Theme.surface2
                            : (modelData.relayed || modelData.conflict) ? Theme.relayBg : Theme.directBg
                        readonly property color badgeFg: (modelData.self || !modelData.verified || modelData.pingMs < 0) ? Theme.muted
                            : (modelData.relayed || modelData.conflict) ? Theme.relayFg : Theme.directFg

                        onClicked: if (!modelData.self && modelData.verified)
                                       Backend.copyText(modelData.address, qsTr("Copied address %1 (%2)").arg(modelData.address).arg(modelData.name))

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
                                Layout.fillWidth: true
                                spacing: 3
                                Text { textFormat: Text.PlainText;
                                    Layout.fillWidth: true
                                    text: row.modelData.name + (row.modelData.owner ? "  ★" : "")
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text { textFormat: Text.PlainText; text: row.modelData.address; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 12 }
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
                    text: qsTr("Invite friends")
                    onClicked: Backend.copyText(Backend.networkCode, qsTr("Network code copied – send it to your friends"))
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
}
