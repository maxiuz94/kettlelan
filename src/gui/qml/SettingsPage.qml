import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Program settings.
Item {
    id: page
    signal back()
    onVisibleChanged: if (!visible) nickField.save() // leaving the settings saves a name still being typed

    component SectionTitle: Text { textFormat: Text.PlainText;
        color: Theme.muted
        font.pixelSize: 13
        font.weight: Font.DemiBold
        Layout.topMargin: 4
    }
    component Group: Rectangle {
        default property alias content: inner.data
        Layout.fillWidth: true
        implicitHeight: inner.implicitHeight
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
        clip: true
        ColumnLayout { id: inner; anchors.left: parent.left; anchors.right: parent.right; spacing: 0 }
    }
    component InfoRow: Rectangle {
        property string title
        property string value
        property bool last: false
        Layout.fillWidth: true
        implicitHeight: 50
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            Text { textFormat: Text.PlainText; text: parent.parent.title; color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            Text { textFormat: Text.PlainText; text: parent.parent.value; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 13 }
        }
        Rectangle { visible: !parent.last; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 6
            IconButton { iconName: "back"; label: qsTr("Back"); onClicked: page.back() }
            Text { textFormat: Text.PlainText; text: qsTr("Settings"); color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold; Layout.fillWidth: true }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight + 2 * Theme.pad
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body
                x: Theme.pad
                y: Theme.pad
                width: parent.width - 2 * Theme.pad
                spacing: 10

                SectionTitle { text: qsTr("Your name") }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: nameColumn.implicitHeight + 28
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border
                    ColumnLayout {
                        id: nameColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Your name in networks"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        TextField {
                            id: nickField
                            Layout.fillWidth: true
                            implicitHeight: 42
                            text: Backend.nickname
                            maximumLength: 32
                            placeholderText: Backend.personaName !== "" ? qsTr("Your Steam name: %1").arg(Backend.personaName) : qsTr("Your Steam name")
                            placeholderTextColor: Theme.muted
                            color: Theme.text
                            font.pixelSize: 14
                            selectByMouse: true
                            leftPadding: 12
                            Accessible.name: qsTr("Your name in networks")
                            background: Rectangle {
                                radius: Theme.radius
                                color: Theme.bg
                                border.width: nickField.activeFocus ? 2 : 1
                                border.color: nickField.activeFocus ? Theme.accent : Theme.border
                            }
                            // saved by itself a moment after typing (no Enter needed), and when leaving the settings
                            function save() {
                                saveTimer.stop()
                                if (text.trim() === Backend.nickname) return
                                Backend.nickname = text
                                savedTimer.restart()
                            }
                            onTextEdited: saveTimer.restart()
                            onEditingFinished: save()
                            Timer { id: saveTimer; interval: 800; onTriggered: nickField.save() }
                            Timer { id: savedTimer; interval: 2000 }
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            text: savedTimer.running ? qsTr("Saved ✓") : qsTr("Shown instead of your Steam name to everyone in your networks. Leave it empty to use your Steam name. People in your networks can still see your Steam account in Steam.")
                            color: savedTimer.running ? Theme.ok : Theme.muted
                        }
                    }
                }

                SectionTitle { text: qsTr("General") }
                Group {
                    Toggle {
                        Layout.fillWidth: true
                        title: qsTr("Start with the system")
                        hint: qsTr("Starts hidden in the system tray")
                        checked: Backend.autostart
                        onUserToggled: (value) => Backend.autostart = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        title: qsTr("Reconnect networks automatically")
                        hint: qsTr("On start, switches on the networks that were on when KettleLAN was closed")
                        checked: Backend.autoConnect
                        onUserToggled: (value) => Backend.autoConnect = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        title: qsTr("Closing the window keeps KettleLAN in the tray")
                        hint: Backend.trayAvailable ? qsTr("The network keeps running in the background") : qsTr("Your desktop has no system tray")
                        enabled: Backend.trayAvailable
                        checked: Backend.closeToTray && Backend.trayAvailable
                        onUserToggled: (value) => Backend.closeToTray = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        last: true
                        title: qsTr("Notifications")
                        hint: qsTr("System notifications when someone joins or leaves, and about warnings and errors")
                        checked: Backend.notifications
                        onUserToggled: (value) => Backend.notifications = value
                    }
                }

                SectionTitle { text: qsTr("Privacy and security") }
                Group {
                    Toggle {
                        Layout.fillWidth: true
                        title: qsTr("Hide my IP address")
                        hint: qsTr("Always connect through Steam's servers – nobody in the network learns your address. Ping may increase by a few milliseconds.")
                        checked: Backend.hideAddress
                        onUserToggled: (value) => Backend.hideAddress = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        last: true
                        title: qsTr("Protect this computer's services")
                        hint: qsTr("Blocks network access to SSH, file sharing, remote desktop, databases and admin panels. Games work normally. Blocked attempts: %1").arg(Backend.blockedCount)
                        checked: Backend.protectServices
                        onUserToggled: (value) => Backend.protectServices = value
                    }
                }

                // only when someone is blocked
                SectionTitle { visible: Backend.blockedPeople.length > 0; text: qsTr("Blocked people") }
                Group {
                    visible: Backend.blockedPeople.length > 0
                    Repeater {
                        model: Backend.blockedPeople
                        delegate: Rectangle {
                            id: blockedRow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 58
                            color: "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 10
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { textFormat: Text.PlainText; text: blockedRow.modelData.name; color: Theme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { textFormat: Text.PlainText; text: blockedRow.modelData.address; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 12 }
                                }
                                AppButton { kind: "secondary"; implicitHeight: 38; text: qsTr("Unblock"); Accessible.name: qsTr("Unblock %1").arg(blockedRow.modelData.name)
                                    onClicked: Backend.unblockPerson(blockedRow.modelData.steamId) }
                            }
                            Rectangle { visible: blockedRow.index < Backend.blockedPeople.length - 1; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                        }
                    }
                }

                SectionTitle { text: qsTr("Appearance") }
                Group {
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        Layout.leftMargin: 14
                        Text { textFormat: Text.PlainText; text: qsTr("Theme"); color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
                        ComboBox {
                            id: themeBox
                            implicitWidth: 170
                            implicitHeight: 40
                            textRole: "label"
                            valueRole: "value"
                            model: [
                                { label: qsTr("Same as system"), value: "system" },
                                { label: qsTr("Dark"), value: "dark" },
                                { label: qsTr("Light"), value: "light" }
                            ]
                            Component.onCompleted: currentIndex = indexOfValue(Backend.theme)
                            onActivated: Backend.theme = currentValue
                            Accessible.name: qsTr("Theme")
                            background: Rectangle { radius: Theme.smallRadius + 2; color: Theme.bg; border.color: themeBox.visualFocus ? Theme.accent : Theme.border }
                            contentItem: Text { textFormat: Text.PlainText; leftPadding: 12; text: themeBox.displayText; color: Theme.text; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                SectionTitle { text: qsTr("Advanced") }
                Group {
                    InfoRow { title: qsTr("Network adapter"); value: Backend.interfaceName }
                    InfoRow { title: qsTr("Your address"); value: Backend.selfAddress !== "" ? Backend.selfAddress : "—" }
                    InfoRow { title: "MTU"; value: "1200"; last: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 10
                    AppButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Source code"); onClicked: Backend.openUrl("https://github.com/maxiuz94/kettlelan") }
                    AppButton { Layout.fillWidth: true; kind: "secondary"; text: qsTr("Open log"); onClicked: Backend.openLogs() }
                }
                Text { textFormat: Text.PlainText;
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    font.pixelSize: 12
                    text: qsTr("KettleLAN %1 · BSD-3 license · based on PartyLAN.\nDeveloped with the help of Claude (Anthropic).\nSteam is a trademark of Valve Corporation.").arg(Backend.version)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "<a href=\"https://github.com/maxiuz94/kettlelan/blob/main/THIRD-PARTY-NOTICES.md\">" + qsTr("Licenses of third-party components") + "</a>"
                    textFormat: Text.StyledText
                    linkColor: Theme.muted
                    font.pixelSize: 12
                    onLinkActivated: (link) => Backend.openUrl(link)
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
                AppButton {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    kind: "secondary"
                    text: qsTr("Quit KettleLAN")
                    onClicked: Backend.quitApp()
                }
            }
        }
    }
}
