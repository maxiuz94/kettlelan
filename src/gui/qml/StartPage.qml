import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Start screen: create a network or join with a code.
Item {
    id: page
    signal openSettings()
    readonly property bool busy: Backend.netState === "connecting"

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
            IconButton { iconName: "settings"; label: qsTr("Settings"); onClicked: page.openSettings() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.pad
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: steamRow.implicitHeight + 24
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                RowLayout {
                    id: steamRow
                    anchors.fill: parent
                    anchors.margins: 12
                    anchors.leftMargin: 14
                    spacing: 12
                    Rectangle { width: 10; height: 10; radius: 5; color: Theme.ok }
                    ColumnLayout {
                        spacing: 2
                        Text { textFormat: Text.PlainText; text: qsTr("Connected to Steam"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { textFormat: Text.PlainText; text: qsTr("Signed in as %1").arg(Backend.personaName); color: Theme.muted; font.pixelSize: 13 }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
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

            AppButton {
                Layout.fillWidth: true
                implicitHeight: 52
                kind: "primary"
                iconName: "plus"
                text: qsTr("Create network")
                enabled: !page.busy
                onClicked: createDialog.open()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Network code"); color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: codeField
                        Layout.fillWidth: true
                        implicitHeight: 48
                        placeholderText: qsTr("e.g. KTL-50AA-1SZ3-XRK3")
                        placeholderTextColor: Theme.muted
                        color: Theme.text
                        font.family: Theme.mono
                        font.pixelSize: 15
                        selectByMouse: true
                        enabled: !page.busy
                        leftPadding: 14
                        Accessible.name: qsTr("Network code")
                        background: Rectangle {
                            radius: Theme.radius
                            color: Theme.surface
                            border.width: codeField.activeFocus ? 2 : 1
                            border.color: codeField.activeFocus ? Theme.accent : Theme.border
                        }
                        onAccepted: if (joinButton.enabled) joinButton.clicked()
                    }
                    AppButton {
                        id: joinButton
                        kind: "secondary"
                        text: qsTr("Join")
                        enabled: !page.busy && Backend.isCodeValid(codeField.text)
                        onClicked: Backend.joinNetwork(codeField.text)
                    }
                }
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    text: qsTr("You get the code from whoever created the network. It never changes – save it once.")
                    color: Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            // --- connecting
            RowLayout {
                Layout.fillWidth: true
                visible: page.busy
                spacing: 10
                BusyIndicator { implicitWidth: 28; implicitHeight: 28; running: page.busy }
                Text { textFormat: Text.PlainText; text: qsTr("Connecting to the network…"); color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            }

            // --- saved networks
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                visible: Backend.savedNetworks.length > 0 && !page.busy
                Text { textFormat: Text.PlainText; text: qsTr("Your networks"); color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.maximumHeight: savedList.contentHeight + 2
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    clip: true
                    ListView {
                        id: savedList
                        anchors.fill: parent
                        anchors.margins: 1
                        model: Backend.savedNetworks
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: savedRow
                            required property var modelData
                            required property int index
                            width: savedList.width
                            implicitHeight: 60
                            color: "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 8
                                spacing: 8
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text { textFormat: Text.PlainText; text: savedRow.modelData.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { textFormat: Text.PlainText; text: savedRow.modelData.code; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 12 }
                                }
                                AppButton {
                                    kind: "secondary"
                                    implicitHeight: 40
                                    leftPadding: 14
                                    rightPadding: 14
                                    font.pixelSize: 14
                                    text: qsTr("Connect")
                                    onClicked: Backend.connectSaved(savedRow.modelData.code)
                                }
                                IconButton {
                                    iconName: "trash"
                                    iconSize: 18
                                    label: qsTr("Forget network")
                                    onClicked: Backend.forgetNetwork(savedRow.modelData.code)
                                }
                            }
                            Rectangle {
                                visible: savedRow.index < savedList.count - 1
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.border
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true; visible: Backend.savedNetworks.length === 0 || page.busy }
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
}
