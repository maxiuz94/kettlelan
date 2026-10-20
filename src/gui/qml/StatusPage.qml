import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Ekran przed gotowością: łączenie ze Steamem albo błąd do naprawienia.
Item {
    id: page
    readonly property bool busy: Backend.status === "starting"

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.pad, 360)
        spacing: 14

        SvgIcon {
            Layout.alignment: Qt.AlignHCenter
            name: page.busy ? "kettle" : "alert"
            size: 44
            color: page.busy ? Theme.accent : Theme.relayFg
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.Bold
            text: page.busy ? "Łączę ze Steamem…"
                 : Backend.status === "steamMissing" ? "Brak połączenia ze Steamem"
                 : "Nie udało się przygotować karty sieciowej"
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 14
            lineHeight: 1.3
            text: page.busy ? "Przy pierwszym uruchomieniu system poprosi o hasło – to tworzenie wirtualnej karty sieciowej."
                            : Backend.errorText
        }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: page.busy
            visible: page.busy
        }
        AppButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            visible: !page.busy
            text: "Spróbuj ponownie"
            onClicked: Backend.retry()
        }
    }
}
