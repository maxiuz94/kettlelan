import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Home screen: your address, your networks (switch them on and off), create or join a network.
Item {
    id: page
    signal openSettings()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pad
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 10
            SvgIcon { name: "kettle"; size: 28; color: Theme.accent }
            Text { textFormat: Text.PlainText; text: "KettleLAN"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold; Layout.fillWidth: true }
            // Pause: every network off and no Steam connection (no "Spacewar" on Steam); Resume: the same networks on again
            // always in the same place; greyed out when every network is already off
            IconButton {
                enabled: Backend.connectedCount > 0 || Backend.paused
                opacity: enabled ? 1 : 0.35
                iconName: Backend.paused ? "play" : "pause"
                label: Backend.paused ? qsTr("Resume – switch the paused networks on again")
                     : enabled ? qsTr("Pause – switch every network off and disconnect from Steam")
                     : qsTr("Pause – every network is already off")
                onClicked: Backend.paused ? Backend.resume() : Backend.pause()
            }
            IconButton { iconName: "settings"; label: qsTr("Settings"); onClicked: page.openSettings() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.pad
            spacing: 14

            InfoCard {
                Layout.fillWidth: true
                label: qsTr("Your address · the same in every network")
                value: Backend.selfAddress
                onCopyRequested: Backend.copyText(Backend.selfAddress, qsTr("Your address was copied"))
            }

            // first start: what KettleLAN is for
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 6
                visible: Backend.networks.length === 0
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: qsTr("Play with friends like on a LAN")
                    color: Theme.text
                    font.pixelSize: 23
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: qsTr("Create a network and send its code to your friends, or join an existing one.")
                    color: Theme.muted
                    font.pixelSize: 14
                    lineHeight: 1.3
                    wrapMode: Text.WordWrap
                }
            }

            // --- your networks
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: Backend.networks.length > 0
                Text { textFormat: Text.PlainText; text: qsTr("Your networks"); color: Theme.text; font.pixelSize: 14; font.weight: Font.Bold; Layout.fillWidth: true }
                Text { textFormat: Text.PlainText; text: qsTr("%1 of %2 connected").arg(Backend.connectedCount).arg(Backend.maxNetworks); color: Theme.muted; font.pixelSize: 12 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // as tall as its rows (a Column knows its height from the start); scrolls when they do not fit
                Layout.preferredHeight: rows.implicitHeight + 2
                Layout.maximumHeight: rows.implicitHeight + 2
                visible: Backend.networks.length > 0
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                clip: true
                Flickable {
                    id: networkList
                    anchors.fill: parent
                    anchors.margins: 1
                    contentHeight: rows.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    Column {
                    id: rows
                    width: networkList.width
                    Repeater {
                    id: networkRepeater
                    model: Backend.networks
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool connected: modelData.state === "connected"
                        readonly property bool connecting: modelData.state === "connecting"
                        readonly property bool on: modelData.state !== "off"
                        width: rows.width
                        implicitHeight: Math.max(62, texts.implicitHeight + 20)
                        height: implicitHeight
                        color: openArea.containsMouse ? Theme.surface2 : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: row.connected ? Theme.ok : (row.connecting ? Theme.accent : Theme.border)
                                SequentialAnimation on opacity {
                                    running: row.connecting
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.3; duration: 500 }
                                    NumberAnimation { to: 1; duration: 500 }
                                }
                            }
                            ColumnLayout {
                                id: texts
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: row.modelData.name !== "" ? row.modelData.name : qsTr("Unnamed network")
                                    color: row.modelData.name !== "" ? Theme.text : Theme.muted
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    font.italic: row.modelData.name === ""
                                }
                                Text {
                                    visible: row.modelData.alias !== ""
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: row.modelData.alias
                                    color: Theme.accentText
                                    font.pixelSize: 12
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    font.family: row.on ? Qt.application.font.family : Theme.mono
                                    text: row.connected ? (row.modelData.members === 1 ? qsTr("Connected · 1 member") : qsTr("Connected · %1 members").arg(row.modelData.members))
                                        : row.connecting ? qsTr("Connecting…")
                                        : row.modelData.code
                                }
                            }
                            Text { textFormat: Text.PlainText; text: "›"; color: Theme.muted; font.pixelSize: 22 }
                            Switch {
                                id: sw
                                checked: row.on
                                enabled: row.on || Backend.connectedCount < Backend.maxNetworks
                                opacity: enabled ? 1 : 0.4
                                Accessible.name: row.on ? qsTr("Disconnect from %1").arg(row.modelData.name) : qsTr("Connect to %1").arg(row.modelData.name)
                                onToggled: Backend.setNetworkEnabled(row.modelData.code, checked)
                                padding: 0
                                implicitWidth: 44
                                implicitHeight: 44
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
                        // the whole row except the switch: a connected network opens, any other shows its details
                        MouseArea {
                            id: openArea
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width - 56
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (row.connected) Backend.openNetwork(row.modelData.code)
                                else details.openFor(row.modelData.code, row.modelData.name, row.modelData.alias, false, false)
                            }
                        }
                        Rectangle {
                            visible: row.index < networkRepeater.count - 1
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.border
                        }
                    }
                    }
                    }
                }
            }

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: Backend.networks.length > 0
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 12
                text: Backend.paused ? qsTr("Paused – KettleLAN is not connected to Steam. Press ▶ to switch the same networks on again.")
                      : Backend.status === "offline" ? qsTr("KettleLAN connects to Steam when you switch a network on, and disconnects when all are off.")
                      : Backend.connectedCount >= Backend.maxNetworks
                      ? qsTr("You are connected to %1 networks – the most at the same time. Turn one off to connect to another.").arg(Backend.maxNetworks)
                      : qsTr("You can be connected to up to %1 networks at the same time.").arg(Backend.maxNetworks)
            }

            Item { Layout.fillHeight: true; Layout.preferredHeight: 0 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AppButton {
                    Layout.fillWidth: true
                    implicitHeight: 50
                    kind: "primary"
                    iconName: "plus"
                    text: qsTr("Create network")
                    enabled: Backend.connectedCount < Backend.maxNetworks
                    onClicked: createDialog.open()
                }
                AppButton {
                    Layout.fillWidth: true
                    implicitHeight: 50
                    kind: "secondary"
                    text: qsTr("Join with a code")
                    enabled: Backend.connectedCount < Backend.maxNetworks
                    onClicked: joinDialog.open()
                }
            }
        }

        // --- footer
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pad
            Layout.rightMargin: Theme.pad
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            Text { textFormat: Text.PlainText; text: qsTr("Version %1 · open source (BSD-3)").arg(Backend.version); color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true }
            Text {
                text: "<a href=\"https://github.com/maxiuz94/kettlelan\">" + qsTr("Source code") + "</a>"
                textFormat: Text.StyledText
                linkColor: Theme.muted
                font.pixelSize: 12
                onLinkActivated: (link) => Backend.openUrl(link)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }
    }

    // --- new network name dialog
    Popup {
        id: createDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width - 40, 360)
        modal: true
        focus: true
        padding: 20
        onOpened: { nameField.text = Backend.defaultNetworkName(); nameField.selectAll(); nameField.forceActiveFocus() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.border }
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55000000" }

        contentItem: ColumnLayout {
            spacing: 14
            Text { textFormat: Text.PlainText; text: qsTr("New network"); color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
            Text { textFormat: Text.PlainText; text: qsTr("A name helps your friends recognise the network."); color: Theme.muted; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextField {
                id: nameField
                Layout.fillWidth: true
                implicitHeight: 46
                color: Theme.text
                font.pixelSize: 15
                maximumLength: 60
                selectByMouse: true
                leftPadding: 14
                Accessible.name: qsTr("Network name")
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.bg
                    border.width: nameField.activeFocus ? 2 : 1
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                }
                onAccepted: createButton.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                AppButton { kind: "secondary"; text: qsTr("Cancel"); onClicked: createDialog.close() }
                AppButton {
                    id: createButton
                    kind: "primary"
                    text: qsTr("Create")
                    onClicked: { Backend.createNetwork(nameField.text); createDialog.close() }
                }
            }
        }
    }

    // --- join with a code
    Popup {
        id: joinDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(page.width - 40, 380)
        modal: true
        focus: true
        padding: 20
        onOpened: { codeField.text = ""; codeField.forceActiveFocus() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.border }
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55000000" }

        contentItem: ColumnLayout {
            spacing: 14
            Text { textFormat: Text.PlainText; text: qsTr("Join a network"); color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
            Text { textFormat: Text.PlainText; text: qsTr("You get the code from whoever created the network. It never changes – save it once."); color: Theme.muted; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextField {
                id: codeField
                Layout.fillWidth: true
                implicitHeight: 46
                placeholderText: qsTr("e.g. KTL-50AA-1SZ3-XRK3")
                placeholderTextColor: Theme.muted
                color: Theme.text
                font.family: Theme.mono
                font.pixelSize: 15
                selectByMouse: true
                leftPadding: 14
                Accessible.name: qsTr("Network code")
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.bg
                    border.width: codeField.activeFocus ? 2 : 1
                    border.color: codeField.activeFocus ? Theme.accent : Theme.border
                }
                onAccepted: if (joinButton.enabled) joinButton.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                AppButton { kind: "secondary"; text: qsTr("Cancel"); onClicked: joinDialog.close() }
                AppButton {
                    id: joinButton
                    kind: "primary"
                    text: qsTr("Join")
                    enabled: Backend.isCodeValid(codeField.text)
                    onClicked: { Backend.joinNetwork(codeField.text); joinDialog.close() }
                }
            }
        }
    }

    NetworkDialog { id: details }
}
