import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Program settings.
Item {
    id: page
    signal back()

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
                        title: qsTr("Rejoin the last network automatically")
                        hint: qsTr("On start, rejoins the network you did not leave with “Leave”")
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
                        hint: qsTr("When someone joins or leaves while the window is hidden")
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

                SectionTitle { text: qsTr("Appearance") }
                Group {
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        Layout.leftMargin: 14
                        Text { textFormat: Text.PlainText; text: qsTr("Language"); color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
                        ComboBox {
                            id: languageBox
                            implicitWidth: 170
                            implicitHeight: 40
                            textRole: "label"
                            valueRole: "value"
                            // language names are always shown in their own language
                            model: [
                                { label: "English", value: "en" },
                                { label: "Polski", value: "pl" }
                            ]
                            Component.onCompleted: currentIndex = indexOfValue(Backend.language)
                            onActivated: Backend.language = currentValue
                            Accessible.name: qsTr("Language")
                            background: Rectangle { radius: Theme.smallRadius + 2; color: Theme.bg; border.color: languageBox.visualFocus ? Theme.accent : Theme.border }
                            contentItem: Text { textFormat: Text.PlainText; leftPadding: 12; text: languageBox.displayText; color: Theme.text; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
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
                            // the model is rebuilt after a language change – keep the selection
                            onModelChanged: currentIndex = indexOfValue(Backend.theme)
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
                    text: qsTr("KettleLAN %1 · BSD-3 license · based on PartyLAN.\nSteam is a trademark of Valve Corporation.").arg(Backend.version)
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
