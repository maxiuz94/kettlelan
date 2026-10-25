import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import KettleLAN
import KettleLAN.Core

// Screen shown before ready: connecting to Steam or an error to fix.
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
        Text { textFormat: Text.PlainText;
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.Bold
            text: page.busy ? qsTr("Connecting to Steam…")
                 : Backend.status === "steamMissing" ? qsTr("Waiting for Steam…")
                 : qsTr("Could not set up the network adapter")
        }
        Text { textFormat: Text.PlainText;
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: 14
            lineHeight: 1.3
            text: page.busy ? qsTr("On the first start the system asks for your password – this creates the virtual network adapter.")
                 : Backend.status === "steamMissing" ? Backend.errorText + " " + qsTr("KettleLAN connects as soon as Steam is ready.")
                 : Backend.errorText
        }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: page.busy || Backend.status === "steamMissing"
            visible: running
        }
        AppButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            visible: !page.busy
            text: qsTr("Try again")
            onClicked: Backend.retry()
        }
        AppButton {
            Layout.alignment: Qt.AlignHCenter
            visible: Backend.status === "steamMissing" && Backend.steamEnabled
            kind: "secondary"
            text: qsTr("Don't connect to Steam")
            onClicked: Backend.steamEnabled = false
        }
    }
}
