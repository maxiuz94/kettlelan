import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Ekran startowy: utwórz sieć albo dołącz kodem.
Item {
    id: page
    signal openSettings()
    readonly property bool busy: Backend.netState === "creating" || Backend.netState === "joining"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- nagłówek
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pad
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 10
            SvgIcon { name: "kettle"; size: 28; color: Theme.accent }
            Text { text: "KettleLAN"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold; Layout.fillWidth: true }
            IconButton { iconName: "settings"; label: "Ustawienia"; onClicked: page.openSettings() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // --- treść
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
                        Text { text: "Połączono ze Steam"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { text: "Zalogowano jako " + Backend.personaName; color: Theme.muted; font.pixelSize: 13 }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: "Zagraj ze znajomymi jak w LAN-ie"
                    color: Theme.text
                    font.pixelSize: 23
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "Utwórz sieć i wyślij znajomym jej kod albo dołącz do istniejącej sieci."
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
                text: Backend.netState === "creating" ? "Tworzę sieć…" : "Utwórz sieć"
                enabled: !page.busy
                onClicked: createDialog.open()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Kod sieci"; color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: codeField
                        Layout.fillWidth: true
                        implicitHeight: 48
                        placeholderText: "np. KTL-50AA-1SZ3-XRK3"
                        placeholderTextColor: Theme.muted
                        color: Theme.text
                        font.family: Theme.mono
                        font.pixelSize: 15
                        selectByMouse: true
                        enabled: !page.busy
                        leftPadding: 14
                        Accessible.name: "Kod sieci"
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
                        text: Backend.netState === "joining" ? "Dołączam…" : "Dołącz"
                        enabled: !page.busy && Backend.isCodeValid(codeField.text)
                        onClicked: Backend.joinNetwork(codeField.text)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "Kod dostaniesz od osoby, która utworzyła sieć."
                    color: Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.fillHeight: true }
        }

        // --- stopka
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pad
            Layout.rightMargin: Theme.pad
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            Text { text: "Wersja " + Backend.version + ", open source (BSD-3)"; color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true }
            Text {
                text: "<a href=\"https://github.com/maxiuz94/kettlelan\">Kod źródłowy</a>"
                textFormat: Text.StyledText
                linkColor: Theme.muted
                font.pixelSize: 12
                onLinkActivated: (link) => Backend.openUrl(link)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }
    }

    // --- okno nazwy nowej sieci
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
            Text { text: "Nowa sieć"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
            Text { text: "Nazwa pomoże znajomym rozpoznać sieć."; color: Theme.muted; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            TextField {
                id: nameField
                Layout.fillWidth: true
                implicitHeight: 46
                color: Theme.text
                font.pixelSize: 15
                maximumLength: 60
                selectByMouse: true
                leftPadding: 14
                Accessible.name: "Nazwa sieci"
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
                AppButton { kind: "secondary"; text: "Anuluj"; onClicked: createDialog.close() }
                AppButton {
                    id: createButton
                    kind: "primary"
                    text: "Utwórz"
                    onClicked: { Backend.createNetwork(nameField.text); createDialog.close() }
                }
            }
        }
    }
}
