import QtQuick
import QtQuick.Controls.Basic
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
