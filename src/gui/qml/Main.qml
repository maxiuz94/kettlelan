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

    // Lasting warning (e.g. KettleLAN runs as administrator from a folder others can change).
    Rectangle {
        id: warningBanner
        visible: Backend.warningText !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: visible ? warningRow.implicitHeight + 24 : 0
        color: Theme.dark ? "#3a2f18" : "#fff1d6"
        z: 5
        Row {
            id: warningRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10
            SvgIcon {
                name: "alert"
                size: 18
                color: warningLabel.color
                anchors.top: parent.top
                anchors.topMargin: 1
            }
            Text {
                id: warningLabel
                textFormat: Text.PlainText
                width: parent.width - 28
                wrapMode: Text.WordWrap
                text: Backend.warningText
                color: Theme.dark ? "#f2c86a" : "#6b4a00"
                font.pixelSize: 13
                lineHeight: 1.25
            }
        }
    }
    Item {
        id: pageArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: warningBanner.bottom
    }

    StatusPage {
        anchors.fill: pageArea
        visible: !win.settingsOpen && Backend.status !== "ready"
    }
    StartPage {
        anchors.fill: pageArea
        visible: !win.settingsOpen && Backend.status === "ready" && Backend.netState !== "connected"
        onOpenSettings: win.settingsOpen = true
    }
    NetworkPage {
        anchors.fill: pageArea
        visible: !win.settingsOpen && Backend.status === "ready" && Backend.netState === "connected"
        onOpenSettings: win.settingsOpen = true
    }
    SettingsPage {
        anchors.fill: pageArea
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
