import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Ustawienia programu.
Item {
    id: page
    signal back()

    component SectionTitle: Text {
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
            Text { text: parent.parent.title; color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            Text { text: parent.parent.value; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 13 }
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
            IconButton { iconName: "back"; label: "Wróć"; onClicked: page.back() }
            Text { text: "Ustawienia"; color: Theme.text; font.pixelSize: 17; font.weight: Font.Bold; Layout.fillWidth: true }
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

                SectionTitle { text: "Ogólne" }
                Group {
                    Toggle {
                        Layout.fillWidth: true
                        title: "Uruchamiaj z systemem"
                        hint: "Startuje schowany w zasobniku"
                        checked: Backend.autostart
                        onUserToggled: (value) => Backend.autostart = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        title: "Zamknięcie okna chowa do zasobnika"
                        hint: Backend.trayAvailable ? "Sieć działa dalej w tle" : "Twój pulpit nie ma zasobnika systemowego"
                        enabled: Backend.trayAvailable
                        checked: Backend.closeToTray && Backend.trayAvailable
                        onUserToggled: (value) => Backend.closeToTray = value
                    }
                    Toggle {
                        Layout.fillWidth: true
                        last: true
                        title: "Powiadomienia"
                        hint: "Gdy ktoś dołączy lub wyjdzie, a okno jest schowane"
                        checked: Backend.notifications
                        onUserToggled: (value) => Backend.notifications = value
                    }
                }

                SectionTitle { text: "Wygląd" }
                Group {
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        Layout.leftMargin: 14
                        Text { text: "Motyw"; color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
                        ComboBox {
                            id: themeBox
                            implicitWidth: 170
                            implicitHeight: 40
                            textRole: "label"
                            valueRole: "value"
                            model: [
                                { label: "Jak w systemie", value: "system" },
                                { label: "Ciemny", value: "dark" },
                                { label: "Jasny", value: "light" }
                            ]
                            Component.onCompleted: currentIndex = indexOfValue(Backend.theme)
                            onActivated: Backend.theme = currentValue
                            Accessible.name: "Motyw"
                            background: Rectangle { radius: Theme.smallRadius + 2; color: Theme.bg; border.color: themeBox.visualFocus ? Theme.accent : Theme.border }
                            contentItem: Text { leftPadding: 12; text: themeBox.displayText; color: Theme.text; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                SectionTitle { text: "Zaawansowane" }
                Group {
                    InfoRow { title: "Karta sieciowa"; value: Backend.interfaceName }
                    InfoRow { title: "Twój adres"; value: Backend.selfAddress !== "" ? Backend.selfAddress : "—" }
                    InfoRow { title: "MTU"; value: "1200"; last: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 10
                    AppButton { Layout.fillWidth: true; kind: "secondary"; text: "Kod źródłowy"; onClicked: Backend.openUrl("https://github.com/maxiuz94/kettlelan") }
                    AppButton { Layout.fillWidth: true; kind: "secondary"; text: "Otwórz dziennik"; onClicked: Backend.openLogs() }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    font.pixelSize: 12
                    text: "KettleLAN " + Backend.version + ", licencja BSD-3, na bazie PartyLAN.\nSteam jest znakiem towarowym Valve Corporation."
                }
                AppButton {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    kind: "secondary"
                    text: "Zakończ KettleLAN"
                    onClicked: Backend.quitApp()
                }
            }
        }
    }
}
