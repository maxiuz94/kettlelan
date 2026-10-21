import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

ApplicationWindow {
    id: win
    width: 440
    height: 720
    minimumWidth: 360
    minimumHeight: 600
    visible: false // shown from C++ (unless started with --minimized)
    title: Backend.netState === "connected" ? "KettleLAN – " + Backend.networkName : "KettleLAN"
    color: Theme.bg

    property bool settingsOpen: Backend.demoPage === "settings"

    // Colours for built-in controls (drop-down lists, tooltips, scroll bars).
    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface2
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentInk
    palette.toolTipBase: Theme.surface2
    palette.toolTipText: Theme.text
    palette.mid: Theme.border
    palette.dark: Theme.muted
    palette.light: Theme.surface

    onClosing: (close) => {
        if (Backend.closeToTray && Backend.trayAvailable) {
            close.accepted = false
            win.hide()
        } else {
            Backend.quitApp()
        }
    }

    StatusPage {
        anchors.fill: parent
        visible: !win.settingsOpen && Backend.status !== "ready"
    }
    StartPage {
        anchors.fill: parent
        visible: !win.settingsOpen && Backend.status === "ready" && Backend.netState !== "connected"
        onOpenSettings: win.settingsOpen = true
    }
    NetworkPage {
        anchors.fill: parent
        visible: !win.settingsOpen && Backend.status === "ready" && Backend.netState === "connected"
        onOpenSettings: win.settingsOpen = true
    }
    SettingsPage {
        anchors.fill: parent
        visible: win.settingsOpen
        onBack: win.settingsOpen = false
    }

    // First start on a Polish system: offer Polish. Intentionally bilingual, not translated.
    Popup {
        id: languagePrompt
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 40, 380)
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: Backend.askLanguage
        padding: 20
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.border }
        Overlay.modal: Rectangle { color: Theme.dark ? "#99000000" : "#55000000" }
        contentItem: ColumnLayout {
            spacing: 12
            Text { textFormat: Text.PlainText; text: "Język / Language"; color: Theme.text; font.pixelSize: 18; font.weight: Font.Bold }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 14
                lineHeight: 1.3
                text: "Twój system jest po polsku. Czy chcesz używać KettleLAN po polsku?"
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 12
                text: "Język możesz zmienić w każdej chwili w Ustawieniach. / You can change the language at any time in Settings."
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                AppButton { Layout.fillWidth: true; kind: "secondary"; text: "Keep English"; onClicked: Backend.keepEnglish() }
                AppButton { Layout.fillWidth: true; kind: "primary"; text: "Tak, po polsku"; onClicked: Backend.language = "pl" }
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 88
        z: 10
    }
    Connections {
        target: Backend
        function onToast(text) { toast.show(text) }
    }
}
